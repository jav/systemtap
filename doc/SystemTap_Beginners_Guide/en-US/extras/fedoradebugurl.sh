#! /bin/bash
echo -n "Enter nvr of kernel-debuginfo (e.g. 2.6.25-14.fc9.x86_64) " ; \
read NVR; \
BASE=`uname -m` ; \
NVR=`echo $NVR | sed s/.$BASE//` ; \
VERSION=`echo $NVR | awk -F- '{print $1}'` ; \
RELEASE=`echo $NVR | awk -F- '{print $2}'` ; \
echo "http://kojipkgs.fedoraproject.org/\
packages/kernel/$VERSION/$RELEASE/$BASE/"
