#! /bin/bash
pkg="redhat-release"
releasever=`rpm -q --qf "%{version}" $pkg`
base=`uname -m`
echo "ftp://ftp.redhat.com/pub/redhat/linux/\
enterprise/$releasever/en/os/$base/Debuginfo"
