#!/bin/sh

# list the tapsets in the tapsets.tmpl file and properly format the
# file names
if [ ! -f tapsets.tmpl ]; then
	echo "Error: tapsets.tmpl doesn't exist in the current directory!"
	exit 1
fi
grep "\.stp" tapsets.tmpl | grep ! | sed 's/!Itapset\///g' > checkfile2

# change to the tapset directory and check the tapsets there
(cd ../../tapset/; ls -R) | grep "\.stp" > checkfile1

# order the tapset names then diff the files to examine the differences
sort -d checkfile1 | uniq > checkfile1s
sort -d checkfile2 | uniq > checkfile2s
diff checkfile2s checkfile1s > checkdiff
diff checkfile1s checkfile2s > checkdiff1
diff=`less checkdiff | wc -l `
zero='0'
one='1'
if [ "$diff" -gt "$one" ]
then
	docs=`less checkfile2s | wc -l `
	tapsets=`less checkfile1s | wc -l `
	doctapdiff=`expr $docs - $tapsets` # differece between line count
	# if $docs is greater than there are more tapset documents
	# then there are tapsets being used
	# if there are more $tapsets then there are tapsets that have
	# been excluded from the documentation
	if [ "$doctapdiff" -gt "$zero" ]
	then
		echo You have more Documentation than tapsets, specifically:
		less checkdiff1 | grep ">" | sed 's/> //g'
	else
		echo You have Tapsets that are undocumented, specifically:
		less checkdiff | grep ">" | sed 's/> //g'
	fi
else
	echo You have no missing Tapsets from documentation or excessive documentation!
fi

# at the end we need to make sure we remove any files that we created
# change to proper directory first
rm checkfile2s checkfile1 checkfile1s checkdiff checkdiff1 checkfile2
