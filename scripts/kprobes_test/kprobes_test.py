#!/usr/bin/python

# Copyright (C) 2010 Red Hat Inc.
# 
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.

import os
import pickle
import re
import sys
import time
from config_opts import config_opts
from gen_code import gen_module
from run_module import run_module

class BucketSet(object):
    def __init__(self, bucketA=list(), bucketA_result=0,
                 bucketB=list(), bucketB_result=0, buckets=list(),
                 passed=list(), failed=list(), untriggered=list(),
                 unregistered=list(), split=0): 
        # bucketA is the list of probes to test.
        self.bucketA = bucketA
        # bucketA_result tells us what state bucketA is in
        self.bucketA_result = bucketA_result
        # bucketB is the 2nd (optional) list of probes to test
        self.bucketB = bucketB
        # bucketB_result tells us what state bucketB is in
        self.bucketB_result = bucketB_result
        # buckets is the full list of probes to test (originally
        # filled in by reading config_opts['probes_all'])
        self.buckets = buckets
        # passed is the list of probe points that were sucessfully
        # registered and triggered (actually called)
        self.passed = passed
        # failed is the list of probe points that were sucessfully
        # registered but caused a crash.  The probe lists have been
        # bisected and singly eliminated.
        self.failed = failed
        # untriggered is the list of probe points that were sucessfully
        # registered and but never triggered (actually called)
        self.untriggered = untriggered
        # untriggered is the list of probe points that couldn't be
        # registered
        self.unregistered = unregistered
        # split tells us how to split a bucket.  0 means bisect
        # it, anything else is the number of times we've tried to do a
        # 1-by-1 split.
        self.split = split

    def bucketA_result_str(self):
        if self.bucketA_result == 0:
            return "untested"
        elif self.bucketA_result == 1:
            return "succeeded"
        elif self.bucketA_result == 2:
            return "failed"
        else:
            return "UNKNOWN"

    def bucketB_result_str(self):
        if self.bucketB_result == 0:
            return "untested"
        elif self.bucketB_result == 1:
            return "succeeded"
        elif self.bucketB_result == 2:
            return "failed"
        else:
            return "UNKNOWN"

probes = BucketSet()

# Install this script in config_opts['rclocal'] (typically
# /etc/rc.d/rc.local).  Returns true if the line wasn't already there
# (which means this is the 1st time the script has been run).
def register_script(install=True):
    rc = True
    regexp = re.compile("^.+/kprobes_test.py")
    f = open(config_opts['rclocal'], "r+")
    data = ""
    for line in f:
        if not regexp.match(line):
            data += line
        else:
            rc = False
    if install:
        data += "cd %s/ && ./kprobes_test.py &" % os.getcwd()
    f.seek(0, 0)
    f.write(data)
    f.truncate(f.tell())
    f.close()
    return rc

# Because it is very possible that we'll crash the system, we need to
# make sure our data files are written to the disk sucessfully before
# we run a module.  sync_disks() makes sure everything written is
# actually saved.
def sync_disks():
    os.system("sync; sync; sync")
    time.sleep(5)
    return

def read_probe_list():
    global probes

    # If the "pickled" file exists, read it in to recover our state
    if os.path.exists(config_opts['probes_db']):
        print "Reading state..."
        f = open(config_opts['probes_db'])
        p = pickle.Unpickler(f)
        probes = p.load()
        f.close()

    # if the 'probes_all' file exists, create the data from it
    elif os.path.exists(config_opts['probes_all']):
        # Read in the flat file
        f = open(config_opts['probes_all'])
        probe_lines = f.readlines()
        f.close()
        
        # Create the full bucket
        full_bucket = list()
        for line in probe_lines:
            full_bucket.append(line.rstrip())
        probes.buckets.append(full_bucket)

    # create the probes_all file?
    else:
        print >>sys.stderr, ("Could not find probes file")
        sys.exit(1)
    
def write_probe_list():
    global probes

    print "Writing state..."
    f = open(config_opts['probes_db'], 'w')
    p = pickle.Pickler(f)
    p.dump(probes)
    f.close()

def display_probe_list():
    global probes

    i = 0
    print "bucketA (%s) has %d entries" % \
        (probes.bucketA_result_str(), len(probes.bucketA))
    print "bucketB (%s) has %d entries" % \
        (probes.bucketB_result_str(), len(probes.bucketB))
    for bucket in probes.buckets:
        print "set %d has %d entries" % (i, len(bucket))
        i += 1
    print "passed set has %d entries" % len(probes.passed)
    total = 0
    for bucket in probes.failed:
        total += len(bucket)
    print "failed set has %d entries (in %d buckets)" % \
        (total, len(probes.failed))
    print "untriggered set has %d entries" % len(probes.untriggered)
    print "unregistered set has %d entries" % len(probes.unregistered)

def reset_buckets():
    global probes

    probes.bucketA = list()
    probes.bucketA_result = 0
    probes.bucketB = list()
    probes.bucketB_result = 0
    probes.split = 0

def grab_bucket():
    global probes

    reset_buckets()

    # Try to grab the 1st bucket from the list.
    if len(probes.buckets) > 0:
        bucket = probes.buckets[0]
        del probes.buckets[0]

        # if the bucket has more than 1000 probes, limit it to 1000
        if len(bucket) > 1000:
            probes.bucketA = bucket[0:1000]
            probes.bucketA_result = 0
            probes.bucketB_result = 0

            rest = bucket[1000:]
            probes.buckets.insert(0, rest)

        # otherwise just use the bucket
        else:
            probes.bucketA = bucket
        return True
    else:
        print "no buckets left"
        return False

def split_bucket(bucket):
    global probes

    if probes.split == 0:
        split = len(bucket) / 2
    else:
        split = 1
    bucketA = bucket[0:split]
    bucketB = bucket[split:]

    probes.bucketA = bucketA
    probes.bucketA_result = 0
    probes.bucketB = bucketB
    probes.bucketB_result = 0

def update_buckets(failed=True):
    global probes
    ret = True

    # if we don't have a current set, get one
    if len(probes.bucketA) == 0:
        ret = grab_bucket()

    # if bucketA is set, we've just finished up with it (or bucketB).
    elif len(probes.bucketA) > 0:
        if probes.bucketA_result == 0:
            if not failed:
                probes.bucketA_result = 1
            else:
                probes.bucketA_result = 2
            print "bucketA %s with %d probes..." % \
                (probes.bucketA_result_str(), len(probes.bucketA))
        else:
            if not failed:
                probes.bucketB_result = 1
            else:
                probes.bucketB_result = 2
            print "bucketB %s with %d probes..." % \
                (probes.bucketB_result_str(), len(probes.bucketB))

        # OK, we've got several cases here.
        # (1) Only bucketA was set.
        if len(probes.bucketB) == 0:
            print "case (1)..."
            # (1a) If bucketA passed, put it on the passed list and grab
            # the next bucket.
            if not failed:
                probes.passed.extend(probes.bucketA)
                probes.split = 0
                ret = grab_bucket()

            # (1b) If bucketA failed and is more than 1 probe, split it.
            elif len(probes.bucketA) > 1:
                split_bucket(probes.bucketA)

            # (1c) if bucketA failed and was only 1 probe, put it on
            # the failed list and grab the next bucket.
            else:
                probes.failed.append(probes.bucketA)
                ret = grab_bucket()

        # (2) Both bucketA and bucketB were set, but bucketB hasn't
        # been tested yet.
        elif probes.bucketB_result == 0:
            # Do nothing and let bucketB be tested.
            print "case (2)..."
            pass

        # (3) Both bucketA and bucketB have been tested.  Figure out
        # what to do next.
        else:
            print "case (3)..."

            # (3a) bucketA passed, bucketB failed.  Move bucketA to
            # the passed list and split bucketB.
            if probes.bucketA_result == 1 and probes.bucketB_result == 2:
                print "case (3a)..."
                probes.passed.extend(probes.bucketA)
                probes.split = 0

                # (3a1) If bucketB failed and is more than 1 probe,
                # split it.
                if len(probes.bucketB) > 1:
                    split_bucket(probes.bucketB)

                # (3a2) If bucketB failed and was only 1 probe, put it
                # on the failed list and grab the next bucket.
                else:
                    probes.failed.append(probes.bucketB)
                    ret = grab_bucket()

            # (3b) bucketA failed, bucketB passed.  Move bucketB to
            # the passed list and split bucketA.
            elif probes.bucketA_result == 2 and probes.bucketB_result == 1:
                print "case (3b)..."
                probes.passed.extend(probes.bucketB)
                probes.split = 0

                # (3b1) If bucketA failed and is more than 1 probe,
                # split it.
                if len(probes.bucketA) > 1:
                    split_bucket(probes.bucketA)

                # (3b2) If bucketA failed and was only 1 probe, put it
                # on the failed list and grab the next bucket.
                else:
                    probes.failed.append(probes.bucketA)
                    ret = grab_bucket()

            # (3c) Both buckets failed.
            elif probes.bucketA_result == 2 and probes.bucketB_result == 2:
                print "case (3c)..."
                # (3c1) bucketA and bucketB were both just 1 probe
                # each.  Put them both on the failed list and grab the
                # next bucket.
                if len(probes.bucketA) == 1 and len(probes.bucketB) == 1:
                    print "case (3c1)..."
                    probes.failed.append(probes.bucketA)
                    probes.failed.append(probes.bucketB)
                    ret = grab_bucket()

                # (3c2) bucketA was just 1 probe, but bucketB was more
                # than 1 probe
                elif len(probes.bucketA) == 1 and len(probes.bucketB) > 1:
                    print "case (3c2)..."
                    probes.failed.append(probes.bucketA)
                    split_bucket(probes.bucketB)
                # (3c3) bucketA was more than 1 probe, but bucketB was
                # just 1 probe
                elif len(probes.bucketA) > 1 and len(probes.bucketB) == 1:
                    print "case (3c3)..."
                    probes.failed.append(probes.bucketB)
                    split_bucket(probes.bucketA)
                # (3c4) Both buckets were more than 1 probe.  In this
                # case, split bucketA and put bucketB back on the main
                # list.  This will cause bucketB to get tested twice.
                else:
                    print "case (3c4)..."
                    probes.buckets.insert(0, probes.bucketB)
                    split_bucket(probes.bucketA)

            # (3d) Both buckets passed.  This sounds good, but this
            # means that together bucketA and bucketB failed, but
            # separately they passed.  So, the combination of the sets
            # is the problem.
            else:
                print "case (3d)..."
                # (3d1) If both buckets are just 1 probe, combine the
                # buckets into one and put it on the failed list.
                # Then grab the next bucket.
                if len(probes.bucketA) == 1 and len(probes.bucketB) == 1:
                    bucket = probes.bucketA + probes.bucketB
                    probes.failed.append(bucket)
                    ret = grab_bucket()

                # (3d2) Combine both buckets here, and try eliminating
                # the probes 1-by-1.  We'll go back to bisecting if we
                # can remove 1 probe from this list or the list is
                # indivisible (case 3d3a).
                elif probes.split == 0:
                    probes.split = 1
                    bucket = probes.bucketA + probes.bucketB
                    split_bucket(bucket)
                # (3d3) We're eliminating the probes 1-by-1 and the
                # last attempt failed.  Reverse the order and try
                # again.
                else:
                    probes.split += 1
                    print "case (3d3): bucketA(%d) bucketB(%d)" % (len(probes.bucketA), len(probes.bucketB))
                    bucket = probes.bucketB + probes.bucketA
                    print "case (3d3): combined bucket(%d)" % (len(bucket))
                    if len(bucket) > 500:
                        print >>sys.stderr, "Error: bucket grew?"
                        return -1
                    
                    # (3d3a) If we've tried every probe singly, and
                    # they all still worked, the combination is still
                    # the problem.  So, we're done trying 1-by-1
                    # elimination.
                    if probes.split > len(bucket):
                        probes.split = 0
                        probes.failed.append(bucket)
                        ret = grab_bucket()
                    # (3d3b) Keep trying 1-by-1 splits.
                    else:
                        split_bucket(bucket)

    write_probe_list()
    return ret

def save_bucket():
    global probes

    f = open(config_opts['probes_current'], 'w')
    if len(probes.bucketA) > 0 and probes.bucketA_result == 0:
        f.write("\n".join(probes.bucketA))
    elif len(probes.bucketB) > 0 and probes.bucketB_result == 0:
        f.write("\n".join(probes.bucketB))
    else:
        print "Error: no bucket to write?"
    f.close()

def parse_module_output():
    global probes

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

    # We're done with the result file
    os.unlink(config_opts['probes_result'])

    # Parse the list of probe points.  Since the result fields haven't
    # been updated yet, pick the 1st bucket that has a status of 0
    # (untested).
    if probes.bucketA_result == 0:
        bucket = probes.bucketA
    else:
        bucket = probes.bucketB
        
    new_bucket = list()
    for probe in bucket:
        if pp.has_key(probe):
            # > 0 == passed (registered and triggered)
            if pp[probe] > 0:
                new_bucket.append(probe)
            # 0 == untriggered
            elif pp[probe] == 0:
                probes.untriggered.append(probe)
            # -1 == unregistered
            elif pp[probe] == -1:
                probes.unregistered.append(probe)
            else:
                print >>sys.stderr, "failed probe %s?" % probe
        else:
            print >>sys.stderr, "Couldn't find %s?" % probe

    # OK, we've gone through and removed all the
    # untriggered/unregistered probes from bucket.  Update the
    # proper bucket.
    if probes.bucketA_result == 0:
        # Oops, all the probes were unregistered/untriggered.
        if len(new_bucket) == 0:
            # If we've got a bucketB, we'll think it just succeeded.
            # So, put it back on the bucket list so it will get
            # correctly processed.
            if len(probes.bucketB):
                print "Re-inserting bucketB..."
                probes.buckets.insert(0, probes.bucketB)

            # Reset everything
            reset_buckets()
        else:
            probes.bucketA = new_bucket
    else:
        # If bucketB ended up with 0 entries, we need to handle
        # bucketA.
        if len(new_bucket) == 0:
            # bucketA suceeded and bucketB ended up with 0 entries.
            # Put bucketA on the 'passed' list.
            if probes.bucketA_result == 1:
                probes.passed.extend(probes.bucketA)
            # bucketA failed and bucketB ended up with 0 entries.
            # Put bucketA on the bucket list to get processed again.
            # This will cause it to get tested twice, but it is the
            # easiest way out.
            else:
                print "Re-inserting bucketA..."
                probes.buckets.insert(0, probes.bucketA)

            # Reset everything.
            reset_buckets()
        else:
            probes.bucketB = new_bucket

    return 0

def run_tests():
    status = True
    failed = True
    while status:
        status = update_buckets(failed)
        display_probe_list()
        if not status:
            break

        # Generate the module.
        save_bucket()
        rc = gen_module()
        if rc != 0:
            sys.exit(rc)

        # Run the module.
        sync_disks()
        rc = run_module()
        if rc != 0:
            sys.exit(rc)

        # Parse the module output.
        rc = parse_module_output()
        if rc != 0:
            sys.exit(rc)

        # If we're here, the current module was loaded and unloaded
        # successfully.
        failed = False

def dump_output():
    global probes
    
    f = open(config_opts['probes_passed'], 'w')
    if len(probes.passed) > 0:
        f.write("\n".join(probes.passed))
    f.close()

    # probes.failed is a list of lists.
    f = open(config_opts['probes_failed'], 'w')
    for bucket in probes.failed:
        f.write("\n".join(bucket))
        f.write("\n#\n")
    f.close()

    f = open(config_opts['probes_untriggered'], 'w')
    if len(probes.untriggered) > 0:
        f.write("\n".join(probes.untriggered))
    f.close()

    f = open(config_opts['probes_unregistered'], 'w')
    if len(probes.unregistered) > 0:
        f.write("\n".join(probes.unregistered))
    f.close()

# Make sure we're running as root.
if os.getuid() != 0:
    print >>sys.stderr, "Error: this script must be run by root"
    sys.exit(1)

# Register this script.  If this is the 1st time we've been run, start
# from scratch by removing old state files.
if register_script():
    print >>sys.stderr, "Removing old state files..."
    if os.path.exists(config_opts['log_file']):
        os.unlink(config_opts['log_file'])
    if os.path.exists(config_opts['probes_db']):
        os.unlink(config_opts['probes_db'])

# Redirect stdout and stderr
sys.stdout.flush()
sys.stderr.flush()
so = open(config_opts['log_file'], 'a+', 0)           # no buffering
os.dup2(so.fileno(), sys.stdout.fileno())
os.dup2(so.fileno(), sys.stderr.fileno())
    
# Go!
read_probe_list()
run_tests()

# Finish up.
dump_output()
register_script(False)
print >>sys.stderr, "Finished."
