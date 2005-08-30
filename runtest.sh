#! /bin/sh

# Collect stdout/stderr someplace else

if [ ! -d testsuite ]; then
  mkdir -p testsuite
fi

SRCDIR=`dirname $0`
if expr "$SRCDIR" : "/.*" >/dev/null
then 
   true # already absolute, groovy!
else
   SRCDIR="`pwd`/$SRCDIR"
fi
export SRCDIR

SYSTEMTAP_TAPSET=$SRCDIR/tapset
export SYSTEMTAP_TAPSET

SYSTEMTAP_RUNTIME=$SRCDIR/runtime
export SYSTEMTAP_RUNTIME

dn=`dirname $1`
logfile=testsuite/`basename $dn`-`basename $1`

env | grep SYSTEMTAP > $logfile.cmd
echo "$@" >> $logfile.cmd
eval $@ >$logfile.out 2>$logfile.err
rc=$?
echo "rc=$rc" > $logfile.rc

if expr $1 : '.*ok/.*' >/dev/null; then
  if [ $rc -eq 0 ]; then
     rm -f $logfile.*
  fi
else
  if [ $rc -eq 1 ]; then
     rm -f $logfile.*
  fi
fi

exit $rc
