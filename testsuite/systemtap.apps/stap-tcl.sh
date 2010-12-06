#! /bin/sh

set -e

tclreleasemajor="8.6"
tclrelease="8.6b1"
tcldir=`pwd`/tcl/install/

mkdir -p tcl

if [ ! -r tcl$tclrelease-src.tar.gz ] ; then
    wget http://sourceforge.net/projects/tcl/files/Tcl/$tclrelease/tcl$tclrelease-src.tar.gz/download
fi
if [ ! -r tcl$tclrelease-src.tar.gz ] ; then
   echo FAIL: wget tcl$tclrelease-src.tar.gz
   exit
fi

if [ ! -d tcl/src ] ; then
    tar -x -z -f tcl$tclrelease-src.tar.gz
    mv tcl$tclrelease tcl/src
    sed -i '/runAllTests/i\
singleProcess true' tcl/src/tests/all.tcl
    mv tcl/src/tests/obj.test tcl/src/tests/obj.test.1
fi

cd tcl/src/unix
env CPPFLAGS="-I$SYSTEMTAP_INCLUDES" CFLAGS="-g -O2" ./configure --prefix=$tcldir --enable-dtrace 
make -j2
make install

exit 0
