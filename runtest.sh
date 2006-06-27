#! /bin/sh

# Collect stdout/stderr someplace else

if [ ! -d testresults ]; then
  mkdir -p testresults
fi

SRCDIR=`dirname $0`
case "$SRCDIR" in
/*) ;; # already absolute, groovy!
*) SRCDIR="`pwd`/$SRCDIR" ;;
esac
export SRCDIR

SYSTEMTAP_TAPSET=$SRCDIR/tapset
export SYSTEMTAP_TAPSET

SYSTEMTAP_RUNTIME=$SRCDIR/runtime
export SYSTEMTAP_RUNTIME

if [ -d lib-elfutils ]; then
  lib_elfutils="`pwd`/lib-elfutils"
  elfutils_path="${lib_elfutils}:${lib_elfutils}/systemtap"
  LD_LIBRARY_PATH="${elfutils_path}${LD_LIBRARY_PATH:+:}$LD_LIBRARY_PATH"
  export LD_LIBRARY_PATH
fi

dn=`dirname $1`
logfile=testresults/`basename $dn`-`basename $1`

env | grep SYSTEMTAP > $logfile.cmd
echo "$*" >> $logfile.cmd
# This is proper quoting to let multiword arguments through (for e.g. -e).
"$@" >$logfile.out 2>$logfile.err
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
