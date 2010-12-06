#!/usr/bin/python

# Copyright (C) 2008, 2010 Red Hat Inc.
# 
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.

import os
import sys
from config_opts import config_opts

def gen_files(dir, subset):
    f = open('%s/kprobe_defs.h' % dir, 'w')

    # Output the array of kp_data structs
    print >>f, "static struct kp_data kp_data[] = {"
    i = 0
    while i < len(subset):
        print >>f, ("\t{ .kp={ .symbol_name=\"%s\", .pre_handler=&handler_pre, }, .use_count=ATOMIC_INIT(0) },"
                    % (subset[i]))
        i += 1
    print >>f, "};"
    print >>f
    f.close()

    # Generate the Makefile
    f = open('Makefile', 'w')
    print >>f, """
EXTRA_CFLAGS :=
EXTRA_CFLAGS += -freorder-blocks
EXTRA_CFLAGS += -Wno-unused -Werror
obj-m := kprobe_module.o"""
    f.close()

def run_make_cmd(cmd):
    # Before running make, fix up the environment a bit.  Clean out a
    # few variables that /lib/modules/${KVER}/build/Makefile uses.
    os.unsetenv("ARCH")
    os.unsetenv("KBUILD_EXTMOD")
    os.unsetenv("CROSS_COMPILE")
    os.unsetenv("KBUILD_IMAGE")
    os.unsetenv("KCONFIG_CONFIG")
    os.unsetenv("INSTALL_PATH");

    print "Running", cmd
    return os.system(cmd)

def gen_module():
    f = open(config_opts['probes_current'])
    probes = f.readlines()
    f.close()
    if len(probes) == 0:
        print >>sys.stderr, ("Error: no probe points in %s"
                             % config_opts['probes_current'])
        return -1

    # Cleanup each probe by stripping whitespace
    i = 0
    while i < len(probes):
        probes[i] = probes[i].rstrip()
        i += 1

    # Generate necessary files
    gen_files(os.getcwd(), probes)

    # Try to build the module - add "V=1" at the end for more verbosity
    os.system('rm -f ./kprobe_module.ko')
    (sysname, nodename, release, version, machine) = os.uname()
    cmd = ("make -C \"/lib/modules/%s/build\" M=\"%s\" modules CONFIG_MODULE_SIG=n >build.log 2>&1"
           % (release, os.getcwd()))
    rc = run_make_cmd(cmd)
    if os.WEXITSTATUS(rc) != 0:
        print >>sys.stderr, "Error: Make failed, see build.log for details"
        return -1
    return 0

def main():
    rc = gen_module()
    sys.exit(rc)

if __name__ == "__main__":
    main()
