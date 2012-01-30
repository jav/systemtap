// -*- C++ -*-
// Copyright (C) 2012 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef CMDLINE_H
#define CMDLINE_H 1
extern "C" {
#include <getopt.h>
}

// NB: when adding new options, consider very carefully whether they
// should be restricted from stap clients (after --client-options)!
#define LONG_OPT_KELF 1
#define LONG_OPT_KMAP 2
#define LONG_OPT_IGNORE_VMLINUX 3
#define LONG_OPT_IGNORE_DWARF 4
#define LONG_OPT_VERBOSE_PASS 5
#define LONG_OPT_SKIP_BADVARS 6
#define LONG_OPT_UNPRIVILEGED 7
#define LONG_OPT_OMIT_WERROR 8
#define LONG_OPT_CLIENT_OPTIONS 9
#define LONG_OPT_HELP 10
#define LONG_OPT_DISABLE_CACHE 11
#define LONG_OPT_POISON_CACHE 12
#define LONG_OPT_CLEAN_CACHE 13
#define LONG_OPT_COMPATIBLE 14
#define LONG_OPT_LDD 15
#define LONG_OPT_USE_SERVER 16
#define LONG_OPT_LIST_SERVERS 17
#define LONG_OPT_TRUST_SERVERS 18
#define LONG_OPT_ALL_MODULES 19
#define LONG_OPT_REMOTE 20
#define LONG_OPT_CHECK_VERSION 21
#define LONG_OPT_USE_SERVER_ON_ERROR 22
#define LONG_OPT_VERSION 23
#define LONG_OPT_REMOTE_PREFIX 24
#define LONG_OPT_TMPDIR 25
#define LONG_OPT_DOWNLOAD_DEBUGINFO 26
#define LONG_OPT_DUMP_PROBE_TYPES 27
#define LONG_OPT_PRIVILEGE 28
#define LONG_OPT_SUPPRESS_HANDLER_ERRORS 29
#define LONG_OPT_MODINFO 30

// NB: when adding new options, consider very carefully whether they
// should be restricted from stap clients (after --client-options)!
#define STAP_SHORT_OPTIONS "hVvtp:I:e:o:R:r:a:m:kgPc:x:D:bs:uqwl:d:L:FS:B:WG:"

extern struct option stap_long_options[];
extern int stap_long_opt;

#endif // CMDLINE_H
