#! /bin/bash
pkg=`grep distroverpkg /etc/yum.conf |awk -F= '{print $2}'`
releasever=`rpm -q --qf "%{version}" $pkg`
base=`uname -m`
echo "ftp://ftp.redhat.com/pub/redhat/linux/\
enterprise/$releasever/en/os/$base/Debuginfo"
