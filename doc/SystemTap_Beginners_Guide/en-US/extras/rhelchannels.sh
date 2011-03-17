#! /bin/bash
pkg=`rpm -q --whatprovides "redhat-release"`
releasever=`rpm -q --qf "%{version}" $pkg`
variant=`echo $releasever | tr -d "[:digit:]" | tr "[:upper:]" "[:lower:]" `
if test -z "$variant"; then
  echo "No Red Hat Enterprise Linux variant (workstation/client/server) found."
  exit 1
fi
version=`echo $releasever | tr -cd "[:digit:]"`
base=`uname -i`
echo "rhel-$base-$variant-$version"
echo "rhel-$base-$variant-$version-debuginfo"
echo "rhel-$base-$variant-optional-$version-debuginfo"
echo "rhel-$base-$variant-optional-$version"
