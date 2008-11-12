#!/bin/bash
# Instead of running the original makefile to build the document, run this script instead
echo -n "Please specify your build target (e.g. html-en-US, pdf-en-US, etc)	"
read TARG
echo -n "Thank you. Now specify any build parameters you'd like to use (skip this step for none).	"
read PARM

cp -a ../../testsuite  en-US/extras/.

make $PARM $TARG

echo "Cleaning sync'd files..."
rm -rf en-US/extras/testsuite
echo "...done."
