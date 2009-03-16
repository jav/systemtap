#!/bin/bash
# This script builds the man pages from comments in tapsets. As such, the man page content 
# generated herein should be in sync with Tapset Reference Guide

# cleanup
rm -rf manpages

# create working directory
mkdir manpages ;

# create list of man pages to generate; should be in sync with Tapset Reference Guide 
cat ../SystemTap_Tapset_Reference/tapsets.tmpl | grep  ^\!Itapset > manpageus ;
sed -i -e 's/\!Itapset\///g' manpageus ;

# copy list of man pages into working directory
for i in `cat manpageus` ; do cp ../../tapset/$i manpages ; done ;

# enter workdir
# rm manpageus ;
cd manpages ;

# copy tapsetdescriptions, then clean
for i in `ls`; do sed -n '/\/\/ <tapsetdescription>/,/\/\/ <\/tapsetdescription>/ s/.*/&/w temp' < $i ; 
mv temp $i.tapsetdescription ; 
sed -i -e 's/\/\/ <tapsetdescription>//g' $i.tapsetdescription ;
sed -i -e 's/\/\/ <\/tapsetdescription>//g' $i.tapsetdescription ;
sed -i -e 's/\/\///g' $i.tapsetdescription ;
done

# strip all tapset files to just comments; but first, make sure all comments are exactly 1 space before *
for i in `ls | grep -v tapsetdescription` ; do sed -i -e 's/^  \*/ \*/g' $i; done ;
for i in `ls | grep -v tapsetdescription` ; do sed -i -e '/^ \*/!d' $i; done ;
# rename all tapsets (remove .stp filename suffix), create templates 
for i in `ls | grep -v tapsetdescription` ; do echo $i > tempname ; sed -i -e 's/.stp//g' tempname ; mv $i `cat tempname` ; mv tempname $i ; done ;
# clean all tapsetdescriptions (remove excess spaces)
# for i in `ls | grep tapsetdescription` ; do  perl -p -i -e 's|^\n||g' $i ; done ;

for i in `ls | grep -v .stp | grep -v tapsetdescription` ; 
do echo ".\" -*- nroff -*-" >> $i.template ;
echo ".TH STAPPROBES.manpagename 5 @DATE@ "IBM"" >> $i.template ;
echo ".SH NAME" >> $i.template ;
echo "stapprobes."`cat $i.stp`" \- systemtap "`cat $i.stp`" probe points" >> $i.template ;
echo " " >> $i.template ;
echo ".\" macros" >> $i.template ;
echo ".de SAMPLE" >> $i.template ;
echo ".br" >> $i.template ;
echo ".RS" >> $i.template ;
echo ".nf" >> $i.template ;
echo ".nh" >> $i.template ;
echo ".." >> $i.template ;
echo ".de ESAMPLE" >> $i.template ;
echo ".hy" >> $i.template ;
echo ".fi" >> $i.template ;
echo ".RE" >> $i.template ;
echo ".." >> $i.template ;
echo " " >> $i.template ;
echo ".SH DESCRIPTION" >> $i.template ;
cat $i.stp.tapsetdescription >> $i.template ;
echo ".P" >> $i.template ;
echo ".TP" >> $i.template ;
done

for i in `ls | grep -v .stp | grep -v tapsetdescription | grep -v template` ; 
do cp $i $i.manpagebody ;
perl -p -i -e 's| \* sfunction|.B|g' $i.manpagebody ;
perl -p -i -e 's| \* probe|.B|g' $i.manpagebody ;
perl -p -i -e 's| -|\n\t|g' $i.manpagebody ;
perl -p -i -e 's|(^\t[^\n]*)\n|$1\n\n.B Arguments:|g' $i.manpagebody ;
perl -p -i -e 's| \* @([^:]*):|.I $1 \n|g' $i.manpagebody ; 
perl -p -i -e 's| \* ([^:]*):|.B $1 \n|g' $i.manpagebody ;
perl -p -i -e 's| \* ||g' $i.manpagebody ;
perl -p -i -e 's|.B Arguments: \*|.B No Arguments:\n\n.B Description:|g' $i.manpagebody ;
perl -p -i -e 's|.B Arguments:.I|.B Arguments:\n.I|g' $i.manpagebody ;
perl -p -i -e 's|^ \*/|\n.P\n.TP|g' $i.manpagebody ;
perl -p -i -e 's|\.I|\n\n.I|g' $i.manpagebody ;
perl -p -i -e 's|.B Context|\n.B Context|g' $i.manpagebody ; 
#perl -p -i -e 's|^[^*]*\*|.P|g' $i.manpagebody ;
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
cat $i.template >> stapprobes.$i.5.in ;
cat $i.manpagebody >> stapprobes.$i.5.in ;
cat footer >> stapprobes.$i.5.in ;
done

# cleanup
for i in `cat manpageus`; do
perl -p -i -e 's|.B Description:/|\n.P\n.TP|g' stapprobes.$i.5.in ; 
done


# perl -p -i -e 's|||g' $i.manpagebody
# use to move marked strings.
# sed -n '/\/\/ <tapsetdescription>/,/\/\/ <\/tapsetdescription>/ s/.*/&/w bleh' < ioscheduler