#!/bin/sh

#check to make sure a tapsets.tmpl file exists
if [ ! -f tapsets.tmpl ]; then
	echo "Error: tapsets.tmpl doesn't exist in the current directory!"
	exit 1
fi
# list the tapsets in the tapsets.tmpl file and properly format the
# file names
grep "\.stp" tapsets.tmpl | grep ! | sed 's/!Itapset\///g' > checkfile2

# change to the tapset directory and check the tapsets there
# check to see if directory is present first
if [ ! -d ../../tapset/ ]; then
	echo "Error: tapsets directory doesn't exist!"
	exit 1
fi
(cd ../../tapset/; ls -R) | grep "\.stp" > checkfile1
#might as well check for the functions that are documented in
#langref now too
grep -H sfunction ../../tapset/* | sed 's/..\/..\/tapset\///g' | cut -d : -f 1 | sort -d | uniq  > tap1

# order the tapset names then diff the files to examine the differences
sort -d checkfile1 | uniq > checkfile1s
sort -d checkfile2 | uniq > checkfile2s
comm -23 checkfile1s checkfile2s > missingdoc
comm -12 missingdoc tap1 > commondoc
comm -23 missingdoc tap1 > missingdoc1
comm -13 checkfile1s checkfile2s > missingtap
zero='0'
one='1'
Missingdoc=`cat missingdoc1 |wc -l `
Missingtap=`cat missingtap | wc -l `
Commondoc=`cat commondoc | wc -l `

if [ "$Missingdoc" -gt "$zero" ]
 then
	echo "You have missing documentation from tapsets in use, specifically:"
	cat missingdoc1
fi

if [ "$Missingtap" -gt "$zero" ]
then
	echo "You have documentation for the following tapsets that don't exist!"
	cat missingtap
fi

if [ "$Commondoc" -gt "$zero" ]
then
	echo "The following tapsets did not appear in tapset.tmpl, but have references in the langref file."
	cat commondoc
fi

rm checkfile2 checkfile2s checkfile1 checkfile1s missingtap missingdoc missingdoc1 commondoc tap1

# at the end we need to make sure we remove any files that we created
# change to proper directory first
