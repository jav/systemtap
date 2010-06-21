#!/usr/bin/python

# Copyright (C) 2008, 2010 Red Hat Inc.
# 
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.

import re
import sys
import os
from config_opts import config_opts

def parse_module_output():
    # Parse the output file, looking for probe points
    pp_re = re.compile(": (-?\d+) (\S+)$")
    f = open(config_opts['probes_result'], 'r')
    pp = dict()
    line = f.readline()
    while line:
        match = pp_re.search(line)
        if match:
            pp[match.group(2)] =  int(match.group(1))
        line = f.readline()
    f.close()

    if len(pp.keys()) == 0:
        print >>sys.stderr, "No data found?"
        return 1

    # Parse the list of probe points.
    f = open(config_opts['probes_current'], 'r')
    passed = open(config_opts['probes_passed'], 'a')
    failed = open(config_opts['probes_failed'], 'a')
    untriggered = open(config_opts['probes_untriggered'], 'a')
    unregistered = open(config_opts['probes_unregistered'], 'a')
    line = f.readline().strip()
    while line:
        if pp.has_key(line):
            if pp[line] > 0:
                passed.write(line + '\n')
            elif pp[line] == 0:
                untriggered.write(line + '\n')
            elif pp[line] == -1:
                unregistered.write(line + '\n')
            else:
                failed.write(line + '\n')
        line = f.readline().strip()
    f.close()
    passed.close()
    failed.close()
    untriggered.close()
    unregistered.close()
    return 0

def main():
    rc = parse_module_output()
    sys.exit(rc)

if __name__ == "__main__":
    main()
