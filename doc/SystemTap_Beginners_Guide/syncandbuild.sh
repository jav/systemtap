#!/bin/bash
# Instead of running the original makefile to build the document, run this script instead
echo -n "Please specify your build target (e.g. html-en-US, pdf-en-US, etc)	"
read TARG
echo -n "Thank you. Now specify any build parameters you'd like to use (skip this step for none).	"
read PARM

tar -cf examplesdir.tar ./../../testsuite/systemtap.examples/* 
mv examplesdir.tar en-US/extras
cd en-US/extras
tar -xf examplesdir.tar 
rm examplesdir.tar
cd ../../

make $PARM $TARG

echo "Cleaning sync'd files..."
cd en-US/extras
rm -rf testsuite
cd ../../
echo "...done."