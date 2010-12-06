#!/usr/bin/python

# Copyright (C) 2008, 2010 Red Hat Inc.
# 
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.

import sys
import os
import os.path
import time
import select
from config_opts import config_opts

def run_module():
    # Find the current size of /var/log/messages
    logfile = '/var/log/messages'
    print >>sys.stderr, "Getting size of %s..." % logfile

    start_pos = os.path.getsize(logfile)
    l = open(logfile, 'r')
    l.seek(start_pos)

    # Insert the module
    print >>sys.stderr, "Inserting module..."
    rc = os.system("/sbin/insmod %s" % os.path.join(os.getcwd(),
                                                    'kprobe_module.ko'))
    if os.WEXITSTATUS(rc) != 0:
        # This is actually semi-OK, which is why there is no error
        # message here, only a notice.  This might mean that every
        # probe tried cannot be registered.
        print >>sys.stderr, "Notice: insmod failed"
    else:
        # Run the load generate commands (if any).  Note we ignore the
        # return values, since we don't really care if the commands
        # succeed or not.
        if config_opts.has_key('load_cmds'):
            print >>sys.stderr, "Running commands..."
            num_waits = 0

            # Redirect the output to 'run_module.log' by modifying
            # what stdout/stderr points to.
            sys.stdout.flush()
            old_stdout = os.dup(sys.stdout.fileno())
            sys.stderr.flush()
            old_stderr = os.dup(sys.stderr.fileno())
            fd = os.open('run_module.log', os.O_CREAT | os.O_WRONLY)
            os.dup2(fd, sys.stdout.fileno())
            os.dup2(fd, sys.stderr.fileno())

            # Run the commands.
            pid_list = list()
            for cmd in config_opts['load_cmds']:
                pid = os.spawnvp(os.P_NOWAIT, cmd[0], cmd)
                pid_list.append(pid)
            for pid in pid_list:
                (childpid, status) = os.waitpid(pid, 0)

            # Restore old value of stdout/stderr.
            os.close(fd)
            os.dup2(old_stdout, sys.stdout.fileno())
            os.dup2(old_stderr, sys.stderr.fileno())

        # Remove the module
        print >>sys.stderr, "Removing module..."
        rc = os.system("/sbin/rmmod kprobe_module")
        if os.WEXITSTATUS(rc) != 0:
            print >>sys.stderr, "Error: rmmod failed"
            return -1

    # Now we have to wait until everything is flushed to the logfile
    print >>sys.stderr, "Looking for output..."
    rc = 0
    f = open(config_opts['probes_result'], 'w')
    attempts = 0
    while 1:
        # Find the ending size of /var/log/messages
        end_pos = os.path.getsize(logfile)

        if end_pos < start_pos:
            # The log files have been rotated.  Read any leftover data,
            # then reopen file.
            data = l.read()
            if data:
                f.write(data)

                # See if we can find 'kprobe_module unloaded' in the
                # data we just read.
                if data.find('kprobe_module unloaded') != -1:
                    break
        
            l.close()
            l = open(logfile, 'r')
            start_pos = 0
            continue

        # Try to wait until data is available
        while 1:
            try:
                input, output, exc = select.select([l.fileno()], [], [], 60)
                break
            except select.error, err:
                if err[0] != EINTR:
                    raise

        # Get the new stuff logged to /var/log/messages
        data = l.read(end_pos - start_pos + 1)
        if not data:
            # ignore EOF
            time.sleep(2)
            attempts += 1
            if attempts < 30:
                continue
            else:
                print >>sys.stderr, "Error: Couldn't find data"
                rc = -1
                break

        # Write results data
        f.write(data)

        # See if we can find 'kprobe_module unloaded' in the data we
        # just read.
        if data.find('kprobe_module unloaded') == -1:
            start_pos = end_pos
        else:
            break

    l.close()
    f.close()
    return rc

def main():
    rc = run_module()
    sys.exit(rc)

if __name__ == "__main__":
    main()
