# Copyright (C) 2008, 2010 Red Hat Inc.
# 
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.

import os
import sys

# Here we set up the default config options.  These can be overridden
# by the config file.
config_opts = dict()

#
# Various file names
#

# PROBES_ALL is the file that contains the full list of kernel
# functions to test.  If it doesn't exist, it can be created by
# readelf.py with a list of all kernel functions.
config_opts['probes_all'] = 'probes.all'

# PROBES_UNREGISTERED is a file that contains the list of kernel
# functions where a kprobe couldn't be registered.
config_opts['probes_unregistered'] = 'probes.unregistered'

# PROBES_UNTRIGGERED is a file that contains the list of kernel
# functions where a kprobe could be registered, but the kernel
# function wasn't actually called.  So, we don't really know whether
# these functions passed or failed.
config_opts['probes_untriggered'] = 'probes.untriggered'

# PROBES_PASSED is a file that contains the list of kernel functions
# where a kprobe was registered and the kerenel function was called
# with no crash.
config_opts['probes_passed'] = 'probes.passed'

# PROBES_FAILED is a file that contains the list of kernel functions
# where a kprobe was registered and the kerenel function was called
# that caused a crash.
config_opts['probes_failed'] = 'probes.failed'

# PROBES_CURRENT is a file that contains the list of kernel functions
# we're about to test.
config_opts['probes_current'] = 'probes.current'

# PROBES_RESULT is a file that contains the results of the current
# test.
config_opts['probes_result'] = 'probe.out'

# PROBES_DB is a file that contains the current state (in python
# 'pickled' format).
config_opts['probes_db'] = 'probes.db'

# LOG_FILE is the file that contains the testing log output.
config_opts['log_file'] = 'kprobes_test.log'

# RCLOCAL is the file we need to edit to get the test to automatically
# run after a reboot.
config_opts['rclocal'] = '/etc/rc.d/rc.local'

# Read in the config file
print "Reading config file..."
cfg = os.path.join(os.getcwd(), 'default.cfg')
if os.path.exists(cfg):
    execfile(cfg)
else:
    print >>sys.stderr, ("Could not find required config file: %s" % cfg)
    sys.exit(1)
