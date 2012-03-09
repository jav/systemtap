// Shared data for parsing the stap command line
// Copyright (C) 2012 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include <cstdlib>
#include "cmdline.h"

// NB: when adding new options, consider very carefully whether they
// should be restricted from stap clients (after --client-options)!
struct option stap_long_options[] = {
  { "kelf", 0, NULL, LONG_OPT_KELF },
  { "kmap", 2, NULL, LONG_OPT_KMAP },
  { "ignore-vmlinux", 0, NULL, LONG_OPT_IGNORE_VMLINUX },
  { "ignore-dwarf", 0, NULL, LONG_OPT_IGNORE_DWARF },
  { "skip-badvars", 0, NULL, LONG_OPT_SKIP_BADVARS },
  { "vp", 1, NULL, LONG_OPT_VERBOSE_PASS },
  { "unprivileged", 0, NULL, LONG_OPT_UNPRIVILEGED },
#define OWE5 "tter"
#define OWE1 "uild-"
#define OWE6 "fu-kb"
#define OWE2 "i-kno"
#define OWE4 "st"
#define OWE3 "w-be"
  { OWE4 OWE6 OWE1 OWE2 OWE3 OWE5, 0, NULL, LONG_OPT_OMIT_WERROR },
  { "client-options", 0, NULL, LONG_OPT_CLIENT_OPTIONS },
  { "help", 0, NULL, LONG_OPT_HELP },
  { "disable-cache", 0, NULL, LONG_OPT_DISABLE_CACHE },
  { "poison-cache", 0, NULL, LONG_OPT_POISON_CACHE },
  { "clean-cache", 0, NULL, LONG_OPT_CLEAN_CACHE },
  { "compatible", 1, NULL, LONG_OPT_COMPATIBLE },
  { "ldd", 0, NULL, LONG_OPT_LDD },
  { "use-server", 2, NULL, LONG_OPT_USE_SERVER },
  { "list-servers", 2, NULL, LONG_OPT_LIST_SERVERS },
  { "trust-servers", 2, NULL, LONG_OPT_TRUST_SERVERS },
  { "use-server-on-error", 2, NULL, LONG_OPT_USE_SERVER_ON_ERROR },
  { "all-modules", 0, NULL, LONG_OPT_ALL_MODULES },
  { "remote", 1, NULL, LONG_OPT_REMOTE },
  { "remote-prefix", 0, NULL, LONG_OPT_REMOTE_PREFIX },
  { "check-version", 0, NULL, LONG_OPT_CHECK_VERSION },
  { "version", 0, NULL, LONG_OPT_VERSION },
  { "tmpdir", 1, NULL, LONG_OPT_TMPDIR },
  { "download-debuginfo", 2, NULL, LONG_OPT_DOWNLOAD_DEBUGINFO },
  { "dump-probe-types", 0, NULL, LONG_OPT_DUMP_PROBE_TYPES },
  { "privilege", 1, NULL, LONG_OPT_PRIVILEGE },
  { "suppress-handler-errors", 0, NULL, LONG_OPT_SUPPRESS_HANDLER_ERRORS },
  { "modinfo", 1, NULL, LONG_OPT_MODINFO },
  { "rlimit-as", 1, NULL, LONG_OPT_RLIMIT_AS },
  { "rlimit-cpu", 1, NULL, LONG_OPT_RLIMIT_CPU },
  { "rlimit-nproc", 1, NULL, LONG_OPT_RLIMIT_NPROC },
  { "rlimit-stack", 1, NULL, LONG_OPT_RLIMIT_STACK },
  { "rlimit-fsize", 1, NULL, LONG_OPT_RLIMIT_FSIZE },
  { "sysroot", 1, NULL, LONG_OPT_SYSROOT },
  { "sysenv", 1, NULL, LONG_OPT_SYSENV },
  { NULL, 0, NULL, 0 }
};
