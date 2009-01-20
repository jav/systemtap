#!/usr/bin/python

# Copyright (C) 2008 Red Hat Inc.
# 
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.

import re
import sys
import os
#import pickle
import subprocess
from config_opts import config_opts

# Read the output of eu-readelf on vmlinux
(sysname, nodename, release, version, machine) = os.uname()
cmd = "eu-readelf --symbols /usr/lib/debug/lib/modules/%s/vmlinux" % release
print "Running", cmd
p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE)
lines = p.stdout.readlines()
p.wait()
if p.returncode != 0:
    print >>sys.stderr, "Error: eu-readelf failed."
    sys.exit(p.returncode)

# Parse the output
kprobes_text_start = 0
kprobes_text_end = 0
syms = dict()
func_re = re.compile("^\s*\d+:\s+([0-9a-f]+)\s+\d+\s+FUNC\s+\S+\s+\S+\s+\d+\s+(\S+)$")
notype_re = re.compile("^\s*\d+:\s+([0-9a-f]+)\s+\d+\s+NOTYPE\s+\S+\s+\S+\s+\d+\s+(\S+)$")
for line in lines:
    match = notype_re.match(line)
    if match:
        addr = match.group(1)
        name = match.group(2)
        if name == "__kprobes_text_start":
            kprobes_text_start = long(addr, 16)
        elif name == "__kprobes_text_end":
            kprobes_text_end = long(addr, 16)
        continue

    match = func_re.match(line)
    if match:
        addr = match.group(1)
        name = match.group(2)
        syms[name] = long(addr, 16)

# Now we've parsed everything.  Now we need to go back and remove all
# symbols between '__kprobes_text_start' and '__kprobes_text_end',
# since they are already protected from kprobes.  We couldn't do this
# in the loop above, since we might encounter symbols that need to be
# removed before we found the start/end of the kprobes section.
if kprobes_text_start == 0 or kprobes_text_end == 0:
    print "Error - didn't find kprobes_test_start(%d) or kprobes_text_end(%d)" \
        % (kprobes_text_start, kprobes_text_end)
    sys.exit(1)

for name in syms.keys():
    if syms[name] >= kprobes_text_start and syms[name] < kprobes_text_end:
        print "deleting", name
        del syms[name]

## Save data
#f = open('%s.syms' % (release), 'w')
#p = pickle.Pickler(f)
#p.dump(syms)
#f.close()

# Write the data out in text format
f = open(config_opts['probes_all'], 'w')
for name in syms.keys():
    print >>f, name
f.close()
