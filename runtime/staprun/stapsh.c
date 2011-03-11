// stapsh - systemtap remote shell
// Copyright (C) 2011 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
//

// stapsh implements a minimal protocol for a remote stap client to transfer a
// systemtap module to a temporary location and invoke staprun on it.  It is
// not meant to be invoked directly by the user.  Commands are simply
// whitespace-delimited strings, terminated by newlines.
//
//   command: stap VERSION
//     reply: stapsh VERSION MACHINE RELEASE
//      desc: This is the initial handshake.  The VERSION exchange is intended
//            to facilitate compatibility checks, in case the protocol needs to
//            change.  MACHINE and RELEASE are reported as given by uname.
//
//   command: file SIZE NAME
//            DATA
//     reply: (none)
//      desc: Create a file of SIZE bytes, called NAME.  The NAME is a basename
//            only, and limited to roughly "[a-z0-9][a-z0-9._]*".  The DATA is
//            read as raw bytes following the command's newline.
//
//   command: run ARG1 ARG2 ...
//     reply: (none)
//      desc: Start staprun with the given quoted-printable arguments.  When
//            the child exits, stapsh will clean up and then exit with the same
//            return code.  Note that whitespace has significance in stapsh
//            command parsing, all tabs, spaces, and newlines must be escaped
//            in the arguments.  Any embedded NIL (=00) will truncate the
//            argument in the actual command invocation.
//
//   command: quit
//     reply: (none)
//      desc: Signal the child process to quit, then cleanup and exit.
//
// If stapsh reaches EOF on its standard input, it will send SIGHUP to the
// child process, wait for completion, then cleanup and exit normally.


#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>


#define STAPSH_TOK_DELIM " \t\r\n"
#define STAPSH_MAX_FILE_SIZE 32000000 // XXX should be cumulative?
#define STAPSH_MAX_ARGS 256


struct stapsh_handler {
    const char* name;
    int (*fn)(void);
};


static int do_hello(void);
static int do_file(void);
static int do_run(void);
static int do_quit(void);

static const int signals[] = {
    SIGHUP, SIGPIPE, SIGINT, SIGTERM, SIGCHLD
};

static const struct stapsh_handler commands[] = {
      { "stap", do_hello },
      { "file", do_file },
      { "run", do_run },
      { "quit", do_quit },
};

static char tmpdir[FILENAME_MAX] = "";

static pid_t staprun_pid = -1;


static void __attribute__ ((noreturn))
cleanup(int status)
{
  // Mask signals, so if called from non-signal context, we
  // won't get a reentry (especially for SIGCHLD).
  unsigned i;
  sigset_t mask;
  sigemptyset (&mask);
  for (i = 0; i < sizeof(signals) / sizeof(*signals); ++i)
    sigaddset (&mask, signals[i]);
  sigprocmask(SIG_BLOCK, &mask, 0);

  if (staprun_pid > 0)
    {
      int rc, ret;
      kill(staprun_pid, SIGHUP);
      ret = waitpid(staprun_pid, &rc, 0);
      if (status == 0)
        {
          if (ret == staprun_pid)
            status = WIFEXITED(rc) ? WEXITSTATUS(rc) : 128 + WTERMSIG(rc);
          else
            status = 2;
        }
    }

  if (tmpdir[0])
    {
      pid_t pid = 0;
      const char* argv[] = {"rm", "-rf", "--", tmpdir, NULL};
      if (chdir("/")) {} // ignore failure, rm will probably work anyway
      if (!posix_spawnp(&pid, argv[0], NULL, NULL,
                        (char* const*)argv, environ))
        waitpid(pid, NULL, 0);
    }

  exit(status);
}

static void
handle_signal(int sig __attribute__ ((unused)))
{
  cleanup(0);
}

static void
setup_signals (void)
{
  unsigned i;
  struct sigaction sa;
  sa.sa_handler = handle_signal;
  sigemptyset (&sa.sa_mask);
  for (i = 0; i < sizeof(signals) / sizeof(*signals); ++i)
    sigaddset (&sa.sa_mask, signals[i]);
  for (i = 0; i < sizeof(signals) / sizeof(*signals); ++i)
    sigaction (signals[i], &sa, NULL);
}


// Decode a quoted-printable string in-place
static int
qpdecode(char* s)
{
  char* o = s;
  while (*s)
    if (*s != '=')
      *o++ = *s++;
    else
      {
        if (s[1] == '\r' || s[1] == '\n')
          s += 2;
        else if (s[1] == '\r' && s[2] == '\n')
          s += 3;
        else if (!s[1] || !s[2])
          {
            fprintf(stderr, "truncated quoted-printable escape\n");
            return 1;
          }
        else
          {
            errno = 0;
            char *end = 0, hex[] = { s[1], s[2], 0 };
            unsigned char c = strtol(hex, &end, 16);
            if (errno || end != hex + 2)
              {
                fprintf(stderr, "invalid quoted-printable escape =%s\n", hex);
                return 1;
              }
            *o++ = c;
            s += 3;
          }
      }
  *o = '\0';
  return 0;
}


static int
do_hello()
{
  // XXX check caller's version compatibility

  struct utsname uts;
  if (uname(&uts))
    return 1;

  printf("stapsh %s %s %s\n", VERSION, uts.machine, uts.release);
  fflush(stdout);

  return 0;
}

static int
do_file()
{
  if (staprun_pid > 0)
    return 1;

  int ret = 0;
  int size = -1;
  const char* arg = strtok(NULL, STAPSH_TOK_DELIM);
  if (arg)
    size = atoi(arg);
  if (size <= 0 || size > STAPSH_MAX_FILE_SIZE)
    {
      fprintf(stderr, "bad file size %d\n", size);
      return 1;
    }

  const char* name = strtok(NULL, STAPSH_TOK_DELIM);
  if (!name)
    {
      fprintf(stderr, "missing file name\n");
      return 1;
    }
  for (arg = name; *arg; ++arg)
    if (!isalnum(*arg) &&
        !(arg > name && (*arg == '.' || *arg == '_')))
      {
        fprintf(stderr, "bad character in file name '%c'\n", *arg);
        return 1;
      }

  FILE* f = fopen(name, "w");
  if (!f)
    {
      fprintf(stderr, "can't open file for writing\n");
      return 1;
    }
  while (size > 0 && ret == 0)
    {
      char buf[1024];
      size_t r = sizeof(buf);
      if ((size_t)size < sizeof(buf))
	r = size;
      r = fread(buf, 1, r, stdin);
      if (!r)
        {
          fprintf(stderr, "error reading file data\n");
          ret = 1;
          break;
        }
      size -= r;

      const char* bufp = buf;
      while (bufp < buf + r)
        {
          size_t w = (buf + r) - bufp;
          w = fwrite(bufp, 1, w, f);
          if (!w)
            {
              fprintf(stderr, "error writing file data\n");
              ret = 1;
              break;
            }
          bufp += w;
        }
    }
  fclose(f);

  return ret;
}

static int
do_run()
{
  if (staprun_pid > 0)
    return 1;

  char staprun[] = BINDIR "/staprun";
  char* args[STAPSH_MAX_ARGS + 1] = { staprun, 0 };
  unsigned nargs = 1;

  char* arg;
  while ((arg = strtok(NULL, STAPSH_TOK_DELIM)))
    {
      if (nargs + 1 > STAPSH_MAX_ARGS)
        {
          fprintf(stderr, "too many args\n");
          return 1;
        }
      if (qpdecode(arg) != 0)
        return 1;
      args[nargs++] = arg;
    }

  int ret = 0;
  posix_spawn_file_actions_t fa;
  if (posix_spawn_file_actions_init(&fa) != 0)
    return 1;

  // no stdin for staprun
  if (posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0) != 0)
    ret = 1;
  else
    {
      pid_t pid;
      ret = posix_spawn(&pid, args[0], &fa, NULL, args, environ);
      if (ret == 0)
        staprun_pid = pid;
      else
        fprintf(stderr, "error launching staprun\n");
    }

  posix_spawn_file_actions_destroy(&fa);
  return ret;
}

static int
do_quit()
{
  cleanup(0);
}

int
main(int argc __attribute__ ((unused)),
     const char* const argv[] __attribute__ ((unused)))
{
  setup_signals();

  umask(0077);
  snprintf(tmpdir, sizeof(tmpdir), "%s/stapsh.XXXXXX",
           getenv("TMPDIR") ?: "/tmp");
  if (!mkdtemp(tmpdir))
    {
      fprintf(stderr, "no tmpdir\n");
      return 2;
    }
  if (chdir(tmpdir))
    {
      fprintf(stderr, "bad tmpdir\n");
      cleanup(2);
    }

  char command[1024];
  while (fgets(command, sizeof(command), stdin))
    {
      int rc = -1;
      unsigned i;
      const char* arg = strtok(command, STAPSH_TOK_DELIM) ?: "(null)";
      for (i = 0; i < sizeof(commands) / sizeof(*commands); ++i)
        if (strcmp(arg, commands[i].name) == 0)
          {
            rc = commands[i].fn();
            if (rc)
              {
                fprintf(stderr, "failed %s, rc=%d\n", arg, rc);
                //cleanup(rc);
              }
            break;
          }
      if (rc < 0)
        {
          fprintf(stderr, "bad command %s\n", arg);
          //cleanup(1);
        }
    }

  cleanup(0);
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */
