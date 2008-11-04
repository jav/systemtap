#! /bin/bash
check_error() { if test $1 != 0; then echo $2; exit $1; fi }

if [ "$#" -lt 1 ]; then
    UNAME=`uname -r` # determine the kernel running on the machine
else
    UNAME=$1 #user passed in uname value
fi
UNAME=`echo $UNAME | sed "s/ //"` #strip out any whitespace
KERNEL="kernel"
for VARIANT in debug kdump PAE xen; do
  TMP=`echo $UNAME | sed s/$VARIANT//`
  if [ "$TMP" != "$UNAME" ]; then
      UNAME=$TMP; KERNEL="kernel-$VARIANT"
  fi
done
KERN_ARCH=`uname -m`
KERN_REV=`echo $UNAME | sed s/.$KERN_ARCH//` # strip arch from uname
CANDIDATES="$KERNEL-$KERN_REV.$KERN_ARCH $KERNEL-devel-$KERN_REV.$KERN_ARCH \
$KERNEL-debuginfo-$KERN_REV.$KERN_ARCH \
kernel-debuginfo-common-$KERN_REV.$KERN_ARCH"
NEEDED=`rpm --qf "%{name}-%{version}-%{release}.%{arch}\n" -q $CANDIDATES | \
    grep "is not installed" | awk '{print $2}'`
if [ "$NEEDED" != "" ]; then
    echo -e "Need to install the following packages:\n$NEEDED"
    if [ `id -u` = "0" ]; then #attempt download and install
	DIR=`mktemp -d` || exit 1
	yumdownloader --enablerepo="*debuginfo*" $NEEDED --destdir=$DIR
	check_error $? "problem downloading rpm(s) $NEEDED"
	rpm --force -ivh $DIR/*.rpm
	check_error $? "problem installing rpm(s) $NEEDED"
	rm -r $DIR #cleanup
    fi
fi
