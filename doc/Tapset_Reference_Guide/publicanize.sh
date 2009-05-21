#!/bin/bash
INFILE="../SystemTap_Tapset_Reference/tapsets.xml"
OUTFILE="en-US/Tapset_Reference_Guide.xml"
TMPFILE=`mktemp` || exit 1
TMPFILE2=`mktemp` || exit 1

do_help()
{ 
   echo "publicanize.sh: usage:
   -?/--help        this message
   -i/--input=file  input file name
   -o/--output=file output file name
" >&2
}


#process optional arguments -i -o
while [ "$#" -ne 0 ]
do
	arg=`printf %s $1 | awk -F= '{print $1}'`
	val=`printf %s $1 | awk -F= '{print $2}'`
	shift
	if test -z "$val"; then
		local possibleval=$1
		printf %s $1 "$possibleval" | grep ^- >/dev/null 2>&1
		if test "$?" != "0"; then
			val=$possibleval
			if [ "$#" -ge 1 ]; then 
				shift
			fi
		fi
	fi

	case "$arg" in
	        -i|--input)
		   INFILE=$val
		   ;;
	        -o|--output)
		   OUTFILE=$val
		   ;;
		-\?|--help)
		   do_help
		   exit 0
		   ;;
		*)
		   echo "Unknown option \"$arg\". See opcontrol --help" >&2
		   exit 1
		   ;;
	esac
done


#copy the generated tapsets.xml 
cp $INFILE $TMPFILE || exit 1

#remove all excess whitespace
sed -i -e 's/^\s*//g' $TMPFILE

#remove marked Intro (starthere to endhere)
sed -i -e '/starthere/,/endhere/d' $TMPFILE

#re-convert programlisting tags
sed -i  -e 's/&lt;programlisting&gt;/<programlisting>/g' $TMPFILE
sed -i  -e 's/&lt;\/programlisting&gt;/<\/programlisting>/g' $TMPFILE

#replace header

cat $TMPFILE | 
perl -p -e 'undef $/;s|<bookinfo>\n<title>SystemTap Tapset Reference Manual</title>|<xi:include href="Book_Info.xml" xmlns:xi="http://www.w3.org/2001/XInclude" />\n<xi:include href="Preface.xml" xmlns:xi="http://www.w3.org/2001/XInclude" />|msg' | 
#perl -p -e 'undef $/;s|<authorgroup>\n<author>\n<othername>SystemTap</othername>\n<contrib>Hackers</contrib>\n</author>\n</authorgroup>||msg' |
#perl -p -e 'undef $/;s|<copyright>\n<year>2008-2009</year>\n<holder>Red Hat, Inc. and others</holder>\n</copyright>||msg' |
#perl -p -e 'undef $/;s|<legalnotice>\n<para>\nThis documentation is free software\; you can redistribute\nit and/or modify it under the terms of the GNU General Public\nLicense version 2 as published by the Free Software Foundation.\n</para>||msg' | 
#perl -p -e 'undef $/;s|<para>\nThis program is distributed in the hope that it will be\nuseful, but WITHOUT ANY WARRANTY; without even the implied\nwarranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\nSee the GNU General Public License for more details.\n</para>||msg' | 
#perl -p -e 'undef $/;s|<para>\nYou should have received a copy of the GNU General Public\nLicense along with this program; if not, write to the Free\nSoftware Foundation, Inc., 59 Temple Place, Suite 330, Boston,\nMA 02111-1307 USA\n</para>||msg' | 
#perl -p -e 'undef $/;s|<para>\nFor more details see the file COPYING in the source\ndistribution of Linux.\n</para>\n</legalnotice>\n</bookinfo>||msg' | 
#perl -p -e 'undef $/;s|<toc></toc>||msg' | 
perl -p -e 'undef $/;s|\n\n\n\n\n\n\n\n\n\n\n\n\n\n||msg' | 
perl -p -e 'undef $/;s|\n\n\n\n\n\n\n\n\n\n\n||msg' |
perl -p -e 'undef $/;s|<programlisting>\n|<programlisting>\n<emphasis>function <\/emphasis>|msg' | 
perl -p -e 'undef $/;s|<para>\n</para>||msg' | 
perl -p -e 'undef $/;s|<para>\n\n</para>||msg' | 
perl -p -e 'undef $/;s|<para>\n<programlisting>|<programlisting>|msg' |
perl -p -e 'undef $/;s|</programlisting>\n</para>|</programlisting>|msg' > $TMPFILE2

#replace Intro with my own
perl -p -i -e 's|<!--markerforxi-->|<xi:include href="Introduction.xml" xmlns:xi="http://www.w3.org/2001/XInclude" />\n<xi:include href="Tapset_Dev_Guide.xml" xmlns:xi="http://www.w3.org/2001/XInclude" />|g' $TMPFILE2

#for tapset name format section
#perl -p -i -e 'undef $/;s|<screen>\nname:return \(parameters\)\ndefinition\n</screen>|<screen>\n<replaceable>function/probe</replaceable> tapset_name:return \(parameters\)\n</screen>|msg' $TMPFILE2
#perl -p -i -e 's|<para>In this guide, tapset definitions appear in the following format:</para>|<para>In this guide, the synopsis of each tapset appears in the following format:</para>|g' $TMPFILE2
#perl -p -i -e 's|<!-- markerforxi pls dont remove -->|<xi:include href="tapsetnameformat-lastpara.xml" xmlns:xi="http://www.w3.org/2001/XInclude" />\n<xi:include href="refentry-example.xml" xmlns:xi="http://www.w3.org/2001/XInclude" />|g' $TMPFILE2

# statements change synopsis tags, as they are still currently unfixed in publican-redhat 
sed -i -e 's/refsynopsisdiv>/refsect1>/g' $TMPFILE2
sed -i -e 's/refsect1>/refsection>/g' $TMPFILE2
sed -i -e 's/synopsis>/programlisting>\n/g' $TMPFILE2

# re-convert tags 

sed -i  -e 's/&lt;emphasis&gt;/<emphasis>/g' $TMPFILE2
sed -i  -e 's/&lt;\/emphasis&gt;/<\/emphasis>/g' $TMPFILE2

sed -i  -e 's/&lt;remark&gt;/<remark>/g' $TMPFILE2
sed -i  -e 's/&lt;\/remark&gt;/<\/remark>/g' $TMPFILE2

sed -i  -e 's/&lt;command&gt;/<command>/g' $TMPFILE2
sed -i  -e 's/&lt;\/command&gt;/<\/command>/g' $TMPFILE2

#useful marker script; moves content between starthere and endhere to file target
#sed -n '/starthere/,/endhere/ s/.*/&/w target'  $TMPFILE2

mv $TMPFILE2 $OUTFILE
