#!/bin/bash
# Instead of running the original makefile to build the document, run this script instead
echo -n "Please specify your build target (e.g. html-en-US, pdf-en-US, etc)	"
read TARG
echo -n "Please specify the product you are building for: enter 1 for Fedora, 2 for RHEL	"
read PROD
echo -n "Thank you. Now specify any build parameters you'd like to use (skip this step for none).	"
read PARM

cp -a ../../testsuite  en-US/extras/.

if [ $PROD = 1 ]; 
	then 	
	sed -i -e 's/<productname>Red Hat Enterprise Linux/<productname>Fedora/g' en-US/Book_Info.xml;
	sed -i -e 's/<productnumber>5/<productnumber>10/g' en-US/Book_Info.xml;
	sed -i -e 's/BRAND = RedHat/BRAND = fedora/g' Makefile;
	make $PARM $TARG

else make $PARM $TARG
fi

echo "Setting default brand back to RHEL (if needed)..." 
	sed -i -e 's/<productname>Fedora/<productname>Red Hat Enterprise Linux/g' en-US/Book_Info.xml;
	sed -i -e 's/<productnumber>10/<productnumber>5/g' en-US/Book_Info.xml;
	sed -i -e 's/BRAND = fedora/BRAND = RedHat/g' Makefile;	
echo "done."
echo "Cleaning sync'd files..."
make post
echo "...done."