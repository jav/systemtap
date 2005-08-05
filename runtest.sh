#! /bin/sh

# Collect stdout/stderr someplace else

if [ ! -d testsuite ]; then
  mkdir -p testsuite
fi

SRCDIR=`dirname $0`
export SRCDIR

SYSTEMTAP_TAPSET=$SRCDIR/tapset
export SYSTEMTAP_TAPSET

SYSTEMTAP_RUNTIME=$SRCDIR/runtime
export SYSTEMTAP_RUNTIME

dn=`dirname $1`
logfile=testsuite/`basename $dn`-`basename $1`

eval $@ >$logfile.out 2>$logfile.err
rc=$?

if expr $1 : '.*ok/.*' >/dev/null; then
  if [ $rc -eq 0 ]; then
     rm -f $logfile.out $logfile.err
  fi
else
  if [ $rc -eq 1 ]; then
     rm -f $logfile.out $logfile.err
  fi
fi

exit $rc
