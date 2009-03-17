#!/bin/bash
# This script builds the man pages from comments in tapsets. As such, the man page content 
# generated herein should be in sync with Tapset Reference Guide

# cleanup
rm -rf workingdir

# create working directory
mkdir workingdir ;

# create list of man pages to generate; should be in sync with Tapset Reference Guide 
cat ../SystemTap_Tapset_Reference/tapsets.tmpl | grep  ^\!Itapset > manpageus ;
sed -i -e 's/\!Itapset\///g' manpageus ;

# copy list of man pages into working directory
for i in `cat manpageus` ; do cp ../../tapset/$i workingdir ; done ;

# enter workdir
cd workingdir ;

# copy tapsetdescriptions, then clean
for i in `cat ../manpageus`; do 
sed -n '/\/\/ <tapsetdescription>/,/\/\/ <\/tapsetdescription>/ s/.*/&/w temp' < $i ; 
mv temp $i.tapsetdescription ; 
sed -i -e 's/\/\/ <tapsetdescription>//g' $i.tapsetdescription ;
sed -i -e 's/\/\/ <\/tapsetdescription>//g' $i.tapsetdescription ;
sed -i -e 's/\/\///g' $i.tapsetdescription ;
done

# strip all tapset files to just comments; but all comments must be exactly 1 space before and after "*"
for i in `cat ../manpageus` ; do sed -i -e 's/^  \*/ \*/g' $i; 
sed -i -e 's/^ \*  / \* /g' $i; 
# mark the start of each probe entry (sub "/**")
perl -p -i -e 's|^/\*\*| *probestart|g' $i; 
sed -i -e '/^ \*/!d' $i; 
# rename all tapsets (remove .stp filename suffix), create templates 
echo $i > tempname ; 
sed -i -e 's/.stp//g' tempname ; 
mv $i `cat tempname` ; mv tempname $i ; 
done ;
# clean all tapsetdescriptions (remove excess spaces)
# for i in `ls | grep tapsetdescription` ; do  perl -p -i -e 's|^\n||g' $i ; done ;

# create man page headers
for i in `ls | grep -v .stp | grep -v tapsetdescription` ; do 
#echo ".\" -*- nroff -*-" >> $i.template ;
echo ".TH STAPPROBES.manpagename 5 @DATE@ "IBM"" >> $i.template ;
echo ".SH NAME" >> $i.template ;
echo "stapprobes."`cat $i.stp`" \- systemtap "`cat $i.stp`" probe points" >> $i.template ;
echo " " >> $i.template ;
echo ".SH DESCRIPTION" >> $i.template ;
cat $i.stp.tapsetdescription >> $i.template ;
echo " " >> $i.template ;
#echo " " >> $i.template ;
echo ".SH PROBES" >> $i.template ;
echo ".br" >> $i.template ;
echo ".P" >> $i.template ;
echo ".TP" >> $i.template ;
done

# MOST IMPORTANT: clean man page body!
for i in `ls | grep -v .stp | grep -v tapsetdescription | grep -v template` ; 
do cp $i $i.tmp ;
perl -p -i -e 's| \* sfunction|.BR|g' $i.tmp ;
perl -p -i -e 's| \* probe|.BR|g' $i.tmp ;
perl -p -i -e 's| -|\ninitlinehere|g' $i.tmp ;
perl -p -i -e 's|^initlinehere([^\n]*)\n|\n.br\n$1\n\n.B Arguments:|g' $i.tmp ;
perl -p -i -e 's| \* @([^:]*):|\n.I $1\n.br\n|g' $i.tmp ; 
perl -p -i -e 's| \* ([^:]*):|\n.BR $1:\n.br\n|g' $i.tmp ;
perl -p -i -e 's|\*probestart|\n.P\n.TP|g' $i.tmp ;
perl -p -i -e 's|\.I|\n\n.I|g' $i.tmp ;
# special formatting for Arguments header
perl -p -i -e 's|.B Arguments: \*\/||g' $i.tmp ;
perl -p -i -e 's|.B Arguments: \*|.B Description:|g' $i.tmp ;

cat $i.tmp | 
perl -p -e 'undef $/;s|.B Arguments:\n.B|.B|msg' |
perl -p -e 'undef $/;s|\n\n\n|\n\n|msg' > $i.manpagebody ; 
done

# generate footer template
mv ../manpageus .
sed -i -e 's/.stp//g' manpageus
echo ".SH SEE ALSO" >> footer
echo ".IR stap (1)," >> footer
echo ".IR stapprobes (5)," >> footer
for i in `cat manpageus`; do echo ".IR stapprobes."$i" (5)," >> footer ; done

# assemble parts
for i in `cat manpageus`; do 
cat $i.template >> $i.5 ;
cat $i.manpagebody >> $i.5 ;
cat footer >> $i.5 ;
done

# cleanup
for i in `cat manpageus`; do
# context.stp 
perl -p -i -e 's|.B Description:/|\n.P\n.TP|g' $i.5 ; 
perl -p -i -e 's|.B Description:|.B Description:\n\n |g' $i.5 ;
cat $i.5 | perl -p -e 'undef $/;s|\.B Arguments:\n\n\.B |.B|msg' | 
perl -p -e 'undef $/;s|\n \* | |msg' > stapprobes.$i.5.in ; 
# cleanup all remaining stars, excess initial whitespace, and trailing "/" per line
perl -p -i -e 's|^ \*||g' stapprobes.$i.5.in;
perl -p -i -e 's|^ ||g' stapprobes.$i.5.in;
perl -p -i -e 's|^/||g' stapprobes.$i.5.in;
# convert tags
perl -p -i -e 's|</[^>]*>|\n|g' stapprobes.$i.5.in ;
perl -p -i -e 's|<[^>]*>|\n.B |g' stapprobes.$i.5.in ;
# cleanup remaining excess whitespace
perl -p -i -e 's|\t\t| |g' stapprobes.$i.5.in;
perl -p -i -e 's|^ ||g' stapprobes.$i.5.in;
#sed -i -e 's/$/ /g' stapprobes.$i.5.in;
#sed -i -e 's|$  | |g' stapprobes.$i.5.in;
done

# file cleanup
rm `ls | grep -v stapprobes`

# perl -p -i -e 's|||g' stapprobes.$i.5.in ;

# perl -p -i -e 's|||g' $i.manpagebody
# use to move marked strings.
# sed -n '/\/\/ <tapsetdescription>/,/\/\/ <\/tapsetdescription>/ s/.*/&/w bleh' < ioscheduler
# remove excess initial whitespace for each line
# perl -p -i -e 's|^ ||g' stapprobes.$i.5.in; 
# convert tags
# perl -p -i -e 's|</[^>]*>|\n|g' stapprobes.$i.5.in ;
# perl -p -i -e 's|<[^>]*>|\n.B |g' stapprobes.$i.5.in ;