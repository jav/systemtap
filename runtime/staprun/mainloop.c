/* -*- linux-c -*-
 *
 * mainloop - stapio main loop
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 *
 * Copyright (C) 2005-2008 Red Hat Inc.
 */

#include "staprun.h"
#include <sys/utsname.h>
#include <sys/ptrace.h>
#include <wordexp.h>


#define WORKAROUND_BZ467568 1  /* PR 6964; XXX: autoconf when able */


/* globals */
int ncpus;
static int use_old_transport = 0;
//enum _stp_sig_type { sig_none, sig_done, sig_detach };
//static enum _stp_sig_type got_signal = sig_none;


static void *signal_thread(void *arg)
{
  sigset_t *s = (sigset_t *) arg;
  int signum, rc, btype = STP_EXIT;

  while (1) {
    if (sigwait(s, &signum) < 0) {
      _perr("sigwait");
      continue;
    }
    dbug(2, "sigproc %d (%s)\n", signum, strsignal(signum));
    if (signum == SIGQUIT)
      cleanup_and_exit(1);
    else if (signum == SIGINT || signum == SIGHUP || signum == SIGTERM) {
      // send STP_EXIT
      rc = write(control_channel, &btype, sizeof(btype));
      break;
    }
  }
  return NULL;
}

static void chld_proc(int signum)
{
  int32_t rc, btype = STP_EXIT;
  dbug(2, "chld_proc %d (%s)\n", signum, strsignal(signum));
  pid_t pid = waitpid(-1, NULL, WNOHANG);
  if (pid != target_pid)
    return;
  // send STP_EXIT
  rc = write(control_channel, &btype, sizeof(btype));
}

#if WORKAROUND_BZ467568
/* Used for pause()-based synchronization. */
static void signal_dontcare(int signum)
{
  (void) signum;
}
#endif

static void setup_main_signals(void)
{
  pthread_t tid;
  struct sigaction sa;
  sigset_t *s = malloc(sizeof(*s));
  if (!s) {
    _perr("malloc failed");
    exit(1);
  }
  sigfillset(s);
  pthread_sigmask(SIG_SETMASK, s, NULL);

  memset(&sa, 0, sizeof(sa));
  sigfillset(&sa.sa_mask);
  sa.sa_handler = SIG_IGN;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGHUP, &sa, NULL);
  sigaction(SIGQUIT, &sa, NULL);

  sa.sa_handler = chld_proc;
  sigaction(SIGCHLD, &sa, NULL);

  sigemptyset(s);
  sigaddset(s, SIGINT);
  sigaddset(s, SIGTERM);
  sigaddset(s, SIGHUP);
  sigaddset(s, SIGQUIT);
  pthread_sigmask(SIG_SETMASK, s, NULL);
  if (pthread_create(&tid, NULL, signal_thread, s) < 0) {
    _perr("failed to create thread");
    exit(1);
  }
}

/*
 * start_cmd forks the command given on the command line with the "-c"
 * option. It will wait just at the cusp of the exec until we get the
 * signal from the kernel to let it run.  We do it this way because we
 * must have the pid of the forked command so it can be set to the
 * module and made available internally as _stp_target.  PTRACE_DETACH
 * is sent from stp_main_loop() below when it receives STP_START from
 * the module.
 */
void start_cmd(void)
{
  pid_t pid;
  struct sigaction a;

  /* if we are execing a target cmd, ignore ^C in stapio */
  /* and let the target cmd get it. */
  sigemptyset(&a.sa_mask);
  a.sa_flags = 0;
  a.sa_handler = SIG_IGN;
  sigaction(SIGINT, &a, NULL);

  if ((pid = fork()) < 0) {
    _perr("fork");
    exit(1);
  } else if (pid == 0) {
    /* We're in the target process.	 Let's start the execve of target_cmd, */
    int rc;
    wordexp_t words;
    char *sh_c_argv[4] = { NULL, NULL, NULL, NULL };

    a.sa_handler = SIG_DFL;
    sigaction(SIGINT, &a, NULL);

    /* Formerly, we just execl'd(sh,-c,$target_cmd).  But this does't
       work well if target_cmd is a shell builtin.  We really want to
       probe a new child process, not a mishmash of shell-interpreted
       stuff. */
    rc = wordexp (target_cmd, & words, WRDE_NOCMD|WRDE_UNDEF);
    if (rc == WRDE_BADCHAR)
      {
        /* The user must have used a shell metacharacter, thinking that
           we use system(3) to evaluate 'stap -c CMD'.  We could generate
           an error message ... but let's just do what the user meant.
           rhbz 467652. */
        sh_c_argv[0] = "sh";
        sh_c_argv[1] = "-c";
        sh_c_argv[2] = target_cmd;
        sh_c_argv[3] = NULL;
      }
    else
      {
        switch (rc)
          {
          case 0:
            break;
          case WRDE_SYNTAX:
            _err ("wordexp: syntax error (unmatched quotes?) in -c COMMAND\n");
            _exit(1);
          default:
            _err ("wordexp: parsing error (%d)\n", rc);
            _exit (1);
          }
        if (words.we_wordc < 1) { _err ("empty -c COMMAND"); _exit (1); }
      }

/* PR 6964: when tracing all the user space process including the
   child the signal will be messed due to uprobe module or utrace
   bug. The kernel sometimes crashes.  So as an alternative
   approximation, we just wait here for a signal from the parent. */

    dbug(1, "blocking briefly\n");
#if WORKAROUND_BZ467568
    {
      /* We use SIGUSR1 here, since pause() only returns if a
         handled signal was received. */
      struct sigaction sa;
      memset(&sa, 0, sizeof(sa));
      sigfillset(&sa.sa_mask);
      sa.sa_handler = signal_dontcare;
      sigaction(SIGUSR1, &sa, NULL);
      pause ();
      sa.sa_handler = SIG_DFL;
      sigaction(SIGUSR1, &sa, NULL);
    }
#else
    rc = ptrace (PTRACE_TRACEME, 0, 0, 0);
    if (rc < 0) perror ("ptrace me");
    raise (SIGCONT); /* Harmless; just passes control to parent. */
#endif

    dbug(1, "execing target_cmd %s\n", target_cmd);

    /* Note that execvp() is not a direct system call; it does a $PATH
       search in glibc.  We would like to filter out these dummy syscalls
       from the utrace events seen by scripts.

       This filtering would be done for us for free, if we used ptrace
       ...  but see PR6964.  XXX: Instead, we could open-code the
       $PATH search here; put the pause() afterward; and run a direct
       execve instead of execvp().  */

    if (execvp ((sh_c_argv[0] == NULL ? words.we_wordv[0] : sh_c_argv[0]),
                (sh_c_argv[0] == NULL ? words.we_wordv    : sh_c_argv)) < 0)
      perror(target_cmd);

      /* (There is no need to wordfree() words; they are or will be gone.) */

    _exit(1);
  } else {
    /* We're in the parent.  The child will parse target_cmd and
       execv() the result.  It will be stopped thereabouts and send us
       a SIGTRAP.  Or rather, due to PR 6964, it will stop itself and wait for
       us to release it. */
    target_pid = pid;
#if WORKAROUND_BZ467568
    /* Do nothing else here; see stp_main_loop's handling of a received STP_START. */
#else
    int status;
    waitpid (target_pid, &status, 0);
    dbug(1, "waited for target_cmd %s pid %d status %x\n", target_cmd, target_pid, (unsigned) status);
#endif
  }
}

/**
 * system_cmd() executes system commands in response
 * to an STP_SYSTEM message from the module. These
 * messages are sent by the system() systemtap function.
 */
void system_cmd(char *cmd)
{
  pid_t pid;

  dbug(2, "system %s\n", cmd);
  if ((pid = fork()) < 0) {
    _perr("fork");
  } else if (pid == 0) {
    if (execl("/bin/sh", "sh", "-c", cmd, NULL) < 0)
      perr("%s", cmd);
    _exit(1);
  }
}

/* This is only used in the old relayfs code */
static void read_buffer_info(void)
{
  char buf[PATH_MAX];
  struct statfs st;
  int fd, len, ret;

  if (!use_old_transport)
    return;

  if (statfs("/sys/kernel/debug", &st) == 0 && (int)st.f_type == (int)DEBUGFS_MAGIC)
    return;

  if (sprintf_chk(buf, "/proc/systemtap/%s/bufsize", modname))
    return;
  fd = open(buf, O_RDONLY);
  if (fd < 0)
    return;

  len = read(fd, buf, sizeof(buf));
  if (len <= 0) {
    perr("Couldn't read bufsize");
    close(fd);
    return;
  }
  ret = sscanf(buf, "%u,%u", &n_subbufs, &subbuf_size);
  if (ret != 2)
    perr("Couldn't read bufsize");

  dbug(2, "n_subbufs= %u, size=%u\n", n_subbufs, subbuf_size);
  close(fd);
  return;
}

/**
 *	init_stapio - initialize the app
 *	@print_summary: boolean, print summary or not at end of run
 *
 *	Returns 0 on success, negative otherwise.
 */
int init_stapio(void)
{
  dbug(2, "init_stapio\n");

  /* create control channel */
  use_old_transport = init_ctl_channel(modname, 1);
  if (use_old_transport < 0) {
    err("Failed to initialize control channel.\n");
    return -1;
  }
  read_buffer_info();

  if (attach_mod) {
    dbug(2, "Attaching\n");
    if (use_old_transport) {
      if (init_oldrelayfs() < 0) {
        close_ctl_channel();
        return -1;
      }
    } else {
      if (init_relayfs() < 0) {
        close_ctl_channel();
        return -1;
      }
    }
    return 0;
  }

  /* fork target_cmd if requested. */
  /* It will not actually exec until signalled. */
  if (target_cmd)
    start_cmd();

  /* Run in background */
  if (daemon_mode) {
    pid_t pid;
    int ret;
    dbug(2, "daemonizing stapio\n");

    /* daemonize */
    ret = daemon(0, 1); /* don't close stdout at this time. */
    if (ret) {
      err("Failed to daemonize stapio\n");
      return -1;
    }

    /* change error messages to syslog. */
    switch_syslog("stapio");

    /* show new pid */
    pid = getpid();
    fprintf(stdout, "%d\n", pid);
    fflush(stdout);

    /* redirect all outputs to /dev/null */
    ret = open("/dev/null", O_RDWR);
    if (ret < 0) {
      err("Failed to open /dev/null\n");
      return -1;
    }
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    dup2(ret, STDOUT_FILENO);
    dup2(ret, STDERR_FILENO);
    close(ret);
  }

  return 0;
}

/* cleanup_and_exit() closed channels, frees memory,
 * removes the module (if necessary) and exits. */
void cleanup_and_exit(int detach)
{
  static int exiting = 0;

  if (exiting)
    return;
  exiting = 1;

  setup_main_signals();

  dbug(1, "detach=%d\n", detach);

  /* NB: We don't really need to wait for child processes.  Any that
     were started by the system() tapset function (system_cmd() above)
     can run loose. Or, a target_cmd (stap -c CMD) may have already started and
     stopped.  */

  /* OTOH, it may be still be running - but there's no need for
     us to wait for it, considering that the script must have exited
     for another reason.  So, we no longer   while(...wait()...);  here.
     XXX: we could consider killing it. */

  if (use_old_transport)
    close_oldrelayfs(detach);
  else
    close_relayfs();

  dbug(1, "closing control channel\n");
  close_ctl_channel();

  if (detach) {
    err("\nDisconnecting from systemtap module.\n" "To reconnect, type \"staprun -A %s\"\n", modname);
  } else {
    const char *staprun = getenv ("SYSTEMTAP_STAPRUN") ?: BINDIR "/staprun";
#define BUG9788_WORKAROUND
#ifndef BUG9788_WORKAROUND
    dbug(2, "removing %s\n", modname);
    if (execlp(staprun, basename (staprun), "-d", modname, NULL) < 0) {
      if (errno == ENOEXEC) {
	char *cmd;
	if (asprintf(&cmd, "%s -d '%s'", staprun, modname) > 0)
	  execl("/bin/sh", "sh", "-c", cmd, NULL);
	free(cmd);
      }
      perror(staprun);
      _exit(1);
    }
#else
    pid_t pid;
    int rstatus;
    struct sigaction sa;

    dbug(2, "removing %s\n", modname);

    // So that waitpid() below will work correctly, we need to clear
    // out our SIGCHLD handler.
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_DFL;
    sigaction(SIGCHLD, &sa, NULL);

    pid = fork();
    if (pid < 0) {
      _perr("fork");
      _exit(-1);
    }

    if (pid == 0) {			/* child process */
      /* Run the command. */
      if (execlp(staprun, basename (staprun), "-d", modname, NULL) < 0) {
	if (errno == ENOEXEC) {
	  char *cmd;
	  if (asprintf(&cmd, "%s -d '%s'", staprun, modname) > 0)
	    execl("/bin/sh", "sh", "-c", cmd, NULL);
	  free(cmd);
	}
	perror(staprun);
	_exit(1);
      }
    }

    /* parent process */
    if (waitpid(pid, &rstatus, 0) < 0) {
      _perr("waitpid");
      _exit(-1);
    }

    if (WIFEXITED(rstatus)) {
      _exit(WEXITSTATUS(rstatus));
    }
    _exit(-1);
#endif
  }
  _exit(0);
}

/**
 *	stp_main_loop - loop forever reading data
 */

int stp_main_loop(void)
{
  ssize_t nb;
  void *data;
  uint32_t type;
  FILE *ofp = stdout;
  char recvbuf[8196];

  setvbuf(ofp, (char *)NULL, _IOLBF, 0);
  setup_main_signals();
  dbug(2, "in main loop\n");

  send_request(STP_READY, NULL, 0);

  /* handle messages from control channel */
  while (1) {
    nb = read(control_channel, recvbuf, sizeof(recvbuf));
    dbug(2, "nb=%d\n", (int)nb);
    if (nb <= 0) {
      if (errno != EINTR)
        _perr("Unexpected EOF in read (nb=%ld)", (long)nb);
      continue;
    }

    type = *(uint32_t *) recvbuf;
    data = (void *)(recvbuf + sizeof(uint32_t));
    nb -= sizeof(uint32_t);

    switch (type) {
#ifdef STP_OLD_TRANSPORT
    case STP_REALTIME_DATA:
      {
        ssize_t bw = write(out_fd[0], data, nb);
        if (bw >= 0 && bw != nb) {
          nb = nb - bw;
          bw = write(out_fd[0], data, nb);
        }
        if (bw != nb) {
          _perr("write error (nb=%ld)", (long)nb);
          cleanup_and_exit(0);
        }
        break;
      }
#endif
    case STP_OOB_DATA:
      eprintf("%s", (char *)data);
      break;
    case STP_EXIT:
      {
        /* module asks us to unload it and exit */
        dbug(2, "got STP_EXIT\n");
        cleanup_and_exit(0);
        break;
      }
    case STP_REQUEST_EXIT:
      {
        /* module asks us to start exiting, so send STP_EXIT */
        dbug(2, "got STP_REQUEST_EXIT\n");
        int32_t rc, btype = STP_EXIT;
        rc = write(control_channel, &btype, sizeof(btype));
        break;
      }
    case STP_START:
      {
        struct _stp_msg_start *t = (struct _stp_msg_start *)data;
        dbug(2, "probe_start() returned %d\n", t->res);
        if (t->res < 0) {
          if (target_cmd)
            kill(target_pid, SIGKILL);
          cleanup_and_exit(0);
        } else if (target_cmd) {
          dbug(1, "detaching pid %d\n", target_pid);
#if WORKAROUND_BZ467568
          /* Let's just send our pet signal to the child
             process that should be waiting for us, mid-pause(). */
          kill (target_pid, SIGUSR1);
#else
          /* Were it not for PR6964, we'd like to do it this way: */
          int rc = ptrace (PTRACE_DETACH, target_pid, 0, 0);
          if (rc < 0)
            {
              perror ("ptrace detach");
              if (target_cmd)
                kill(target_pid, SIGKILL);
              cleanup_and_exit(0);
            }
#endif
        }
        break;
      }
    case STP_SYSTEM:
      {
        struct _stp_msg_cmd *c = (struct _stp_msg_cmd *)data;
        dbug(2, "STP_SYSTEM: %s\n", c->cmd);
        system_cmd(c->cmd);
        break;
      }
    case STP_TRANSPORT:
      {
        struct _stp_msg_start ts;
        if (use_old_transport) {
          if (init_oldrelayfs() < 0)
            cleanup_and_exit(0);
        } else {
          if (init_relayfs() < 0)
            cleanup_and_exit(0);
        }
        ts.target = target_pid;
        send_request(STP_START, &ts, sizeof(ts));
        if (load_only)
          cleanup_and_exit(1);
        break;
      }
    default:
      err("WARNING: ignored message of type %d\n", (type));
    }
  }
  fclose(ofp);
  return 0;
}
