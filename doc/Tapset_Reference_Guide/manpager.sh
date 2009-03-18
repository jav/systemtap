#!/bin/bash
# This script builds the man pages from comments in tapsets. As such, the man page content 
# generated herein should be in sync with Tapset Reference Guide

# cleanup
rm -rf man_pages

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

# create man page headers
for i in `ls | grep -v .stp | grep -v tapsetdescription` ; do 
#echo ".\" -*- nroff -*-" >> $i.template ;
echo ".TH STAPPROBES."$i" 5 @DATE@ "IBM"" >> $i.template ;
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
sed -i -e 's/\.stp$//g' ../manpageus ;
for i in `cat ../manpageus` ; 
do mv $i $i.tmp ;
perl -p -i -e 's| \* sfunction|.BR|g' $i.tmp ;
perl -p -i -e 's| \* probe|.BR|g' $i.tmp ;
perl -p -i -e 's| -|\ninitlinehere|g' $i.tmp ;
perl -p -i -e 's|^initlinehere([^\n]*)\n|$1\n |g' $i.tmp ;
perl -p -i -e 's| \* @([^:]*):|\n.I $1:\n|g' $i.tmp ; 
perl -p -i -e 's| \* ([^:]*):|\n.BR $1:\n|g' $i.tmp ;
perl -p -i -e 's| \* ||g' $i.tmp
perl -p -i -e 's|\*probestart|\n.P\n.TP|g' $i.tmp ;
perl -p -i -e 's|\.I|\n.I|g' $i.tmp ;
# remove empty lines
sed -i -e '/^$/d' $i.tmp ;
sed -i -e '/^$/d' $i.tmp ;
sed -i -e 's/^[ \t]*//g' $i.tmp ;
# process Description headers
perl -p -i -e 's|^\*[^/]|\n.BR Description:\n|g' $i.tmp ;
perl -p -i -e 'undef $/;s|\.BR Description:\n\.BR|.BR|g' $i.tmp ; 
perl -p -i -e 'undef $/;s|\.BR Description:\n\*\/||g' $i.tmp ;
# process Argument headers
perl -p -i -e 'undef $/;s|\n\n.I|\n.br\n.BR Arguments:\n.I|g' $i.tmp ;
# clean up formatting of arguments
perl -p -i -e 's|^.I([^:]*:)|\n.br\n.br\n.IR$1\n.br\n\t|g' $i.tmp ;
done

# make tags work
for i in `cat ../manpageus` ; do 
perl -p -i -e 's|</[^>]*>([^.])|$1\n|g' $i.tmp ;
perl -p -i -e 's|<[^>]*>|\n.B |g' $i.tmp ;
# the previous two statements create excess empty lines, remove some of them
sed -i -e '/^$/d' $i.tmp ;
# increase whitespace between some headers
perl -p -i -e 's|^\.BR ([^:]*:)|\n.br\n.BR $1\n.br\n|g' $i.tmp
done 

# generate footer template
echo ".SH SEE ALSO" >> footer
echo ".IR stap (1)," >> footer
echo ".IR stapprobes (5)," >> footer
for i in `cat ../manpageus`; do echo ".IR stapprobes."$i" (5)," >> footer ; done

# assemble parts
for i in `cat ../manpageus`; do 
cat $i.template >> stapprobes.$i.5 ;
cat $i.tmp >> stapprobes.$i.5 ;
cat footer >> stapprobes.$i.5 ;
# final polish
sed -i -e 's/\*\/$//g' stapprobes.$i.5 ;
done

# cleanup
for i in `ls | grep -v 'stapprobes.*.5'` ; do
rm $i ;
done

rm ../manpageus ;
cd ..
mv workingdir man_pages
echo " "
echo "Finished! man pages generated in ./man_pages."
echo " "