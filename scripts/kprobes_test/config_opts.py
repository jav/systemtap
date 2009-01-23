# Copyright (C) 2008 Red Hat Inc.
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

# Various file names
config_opts['probes_result'] = 'probe.out'
config_opts['probes_all'] = 'probes.all'
config_opts['probes_pending'] = 'probes.pending'
config_opts['probes_current'] = 'probes.current'
config_opts['probes_passed'] = 'probes.passed'
config_opts['probes_failed'] = 'probes.failed'
config_opts['probes_untriggered'] = 'probes.untriggered'
config_opts['probes_unregistered'] = 'probes.unregistered'

# Read in the config file
print "Reading config file..."
cfg = os.path.join(os.getcwd(), 'default.cfg')
if os.path.exists(cfg):
    execfile(cfg)
else:
    print >>sys.stderr, ("Could not find required config file: %s" % cfg)
    sys.exit(1)
