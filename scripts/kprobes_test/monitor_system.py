#!/usr/bin/python

# Copyright (C) 2008 Red Hat Inc.
# 
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.

# This script monitors a remote system that is running the kprobes
# test.  If several consecutive 'ping's fail, the system is rebooted.
#
# This script takes as an argument a config filename, whose contents
# should look like the following:
#
#   config_opts['system_name'] = "SYSTEM_NAME"
#   config_opts['restart_cmds'] = [
#       'CMD1',
#       'CMD2',
#       ]
#
# As an example, here is a config file used when monitoring a kvm
# instance:
#
#   config_opts['system_name'] = "dhcp-148"
#   config_opts['restart_cmds'] = [
#       'sudo virsh destroy kvm-rawhide-64-1',
#       'sudo virsh start kvm-rawhide-64-1',
#       ]

import sys
import os
import time

if len(sys.argv) != 2:
    print >>sys.stderr, "Usage: %s config_file" % sys.argv[0]
    sys.exit(1)
cfg = sys.argv[1]

# Read in the config file
if not os.path.exists(cfg):
    print >>sys.stderr, ("Could not find required config file: %s" % cfg)
    sys.exit(1)

print "Reading config file %s..." % cfg
config_opts = dict()
execfile(cfg)
if not config_opts.has_key('system_name'):
    print >>sys.stderr, "Missing required config opt 'system_name'"
    sys.exit(1)
if not config_opts.has_key('restart_cmds'):
    print >>sys.stderr, "Missing required config opt 'restart_cmds'"
    sys.exit(1)

errors = 0
while 1:
    rc = os.system("ping -c 1 %s" % config_opts['system_name'])
    # If ping worked, system is still up and running.  Wait a minute
    # and try again.
    if os.WEXITSTATUS(rc) == 0:
        time.sleep(60)
        errors = 0

    # If the ping failed, increase the error count.  If we've got 3
    # consecutive errors, assume the machine has crashed and restart
    # it.
    else:
        errors += 1
        if errors < 3:
            time.sleep(30)
        else:
            print >>sys.stderr, "Restarting %s..." % config_opts['system_name']
            # Run each restart command

            for cmd in config_opts['restart_cmds']:
                print >>sys.stderr, "Running '%s'..." % cmd
                os.system(cmd)
            # Sleep for 5 minutes to give the system a chance to boot
            print >>sys.stderr, "Sleeping for 5 minutes..."
            time.sleep(5 * 60)
            errors = 0
            
