#!/bin/bash
# Instead of running the original makefile to build the document, run this script instead
echo -n "Please specify your build target (e.g. html, pdf, or html-single)	"
read TARG
echo -n "Please specify the product you are building for (enter 1 for Fedora, 2 for RHEL)	"
read PROD
#echo -n "Please specify any build parameters you'd like to use (skip this step for none).	"
#read PARM
#copy scripts from testsuite
cp -a ../../testsuite en-US/extras/;

if [ $PROD = 1 ]; 
then 	
	sed -i -e 's/<productname>Red Hat Enterprise Linux/<productname>Fedora/g' en-US/Book_Info.xml;
	sed -i -e 's/<productnumber>5.4/<productnumber>10/g' en-US/Book_Info.xml;
	sed -i -e 's/brand: RedHat/brand: fedora/g' publican.cfg;
	sed -i -e 's/condition: RedHat/condition: fedora/g' publican.cfg;
	publican build --formats=$TARG --langs=en-US;
#	rm -rf en-US/extras/testsuite
	sed -i -e 's/<productname>Fedora/<productname>Red Hat Enterprise Linux/g' en-US/Book_Info.xml;
	sed -i -e 's/<productnumber>10/<productnumber>5.4/g' en-US/Book_Info.xml;


else 
sed -i -e 's/brand: fedora/brand: RedHat/g' publican.cfg;
sed -i -e 's/brand: fedora/brand: RedHat/g' publican.cfg;		
#make post $PARM $TARG-en-US
publican build --formats=$TARG --langs=en-US;
#rm -rf en-US/extras/testsuite
sed -i -e 's/<productname>Fedora/<productname>Red Hat Enterprise Linux/g' en-US/Book_Info.xml;
sed -i -e 's/<productnumber>10/<productnumber>5.4/g' en-US/Book_Info.xml;

fi

echo "done."
echo "Cleaning sync'd files..."
rm -rf en-US/extras/testsuite ;
echo "...done."