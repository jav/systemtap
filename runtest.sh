#! /bin/sh

# Redirect stdout/stderr to /dev/null before invoking the given test

SRCDIR=`dirname $0`
export SRCDIR

exec >/dev/null 2>&1
exec ${1+"$@"}
