#! /bin/sh

# Redirect stdout/stderr to /dev/null before invoking the given test

SRCDIR=`dirname $0`
export SRCDIR

SYSTEMTAP_TAPSET=$SRCDIR/tapset
export SYSTEMTAP_TAPSET

SYSTEMTAP_RUNTIME=$SRCDIR/runtime
export SYSTEMTAP_RUNTIME

exec >/dev/null 2>&1
exec ${1+"$@"}
