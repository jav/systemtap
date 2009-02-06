#!/bin/bash

#copy the automated tapsets.xml 
cp ../SystemTap_Tapset_Reference/tapsets.xml en-US/Tapset_Reference_Guide.xml ;

#remove all excess whitespace
sed -i -e 's/^\s*//g' en-US/Tapset_Reference_Guide.xml ;

#re-convert programlisting tags
sed -i  -e 's/&lt;programlisting&gt;/<programlisting>/g' en-US/Tapset_Reference_Guide.xml;
sed -i  -e 's/&lt;\/programlisting&gt;/<\/programlisting>/g' en-US/Tapset_Reference_Guide.xml;

#replace header

cat en-US/Tapset_Reference_Guide.xml | 
perl -p -e 'undef $/;s|<bookinfo>\n<title>SystemTap Tapset Reference Manual</title>|<xi:include href="Book_Info.xml" xmlns:xi="http://www.w3.org/2001/XInclude" />\n<xi:include href="Preface.xml" xmlns:xi="http://www.w3.org/2001/XInclude" />|msg' | 
perl -p -e 'undef $/;s|<authorgroup>\n<author>\n<firstname>William</firstname>\n<surname>Cohen</surname>\n<contrib></contrib>\n<affiliation>\n<address>\n<email>wcohen\@redhat.com</email>\n</address>\n</affiliation>\n</author>\n</authorgroup>||msg' |
perl -p -e 'undef $/;s|<copyright>\n<year>2008, 2009</year>\n<holder>Red Hat, Inc.</holder>\n</copyright>||msg' |
perl -p -e 'undef $/;s|<legalnotice>\n<para>\nThis documentation is free software\; you can redistribute\nit and/or modify it under the terms of the GNU General Public\nLicense version 2 as published by the Free Software Foundation.\n</para>||msg' | 
perl -p -e 'undef $/;s|<para>\nThis program is distributed in the hope that it will be\nuseful, but WITHOUT ANY WARRANTY; without even the implied\nwarranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\nSee the GNU General Public License for more details.\n</para>||msg' | 
perl -p -e 'undef $/;s|<para>\nYou should have received a copy of the GNU General Public\nLicense along with this program; if not, write to the Free\nSoftware Foundation, Inc., 59 Temple Place, Suite 330, Boston,\nMA 02111-1307 USA\n</para>||msg' | 
perl -p -e 'undef $/;s|<para>\nFor more details see the file COPYING in the source\ndistribution of Linux.\n</para>\n</legalnotice>\n</bookinfo>||msg' | 
perl -p -e 'undef $/;s|<toc></toc>||msg' | 
perl -p -e 'undef $/;s|\n\n\n\n\n\n\n\n\n\n\n\n\n\n||msg' | 
perl -p -e 'undef $/;s|<programlisting>\n|<programlisting>\n<emphasis>(sfunction) <\/emphasis>|msg' | 
perl -p -e 'undef $/;s|<para>\n</para>||msg' | 
perl -p -e 'undef $/;s|<para>\n\n</para>||msg' | 
perl -p -e 'undef $/;s|<para>\n<programlisting>|<programlisting>|msg' |
perl -p -e 'undef $/;s|</programlisting>\n</para>|</programlisting>|msg' > clean.xml

cp clean.xml en-US/Tapset_Reference_Guide.xml
rm clean.xml

# statements change synopsis tags, as they are still currently unfixed in publican-redhat 
sed -i -e 's/refsynopsisdiv>/refsect1>/g' en-US/Tapset_Reference_Guide.xml;
sed -i -e 's/refsect1>/refsection>/g' en-US/Tapset_Reference_Guide.xml;
sed -i -e 's/synopsis>/programlisting>\n/g' en-US/Tapset_Reference_Guide.xml; 

# re-convert tags 

sed -i  -e 's/&lt;emphasis&gt;/<emphasis>/g' en-US/Tapset_Reference_Guide.xml;
sed -i  -e 's/&lt;\/emphasis&gt;/<\/emphasis>/g' en-US/Tapset_Reference_Guide.xml;

sed -i  -e 's/&lt;remark&gt;/<remark>/g' en-US/Tapset_Reference_Guide.xml;
sed -i  -e 's/&lt;\/remark&gt;/<\/remark>/g' en-US/Tapset_Reference_Guide.xml;

sed -i  -e 's/&lt;command&gt;/<command>/g' en-US/Tapset_Reference_Guide.xml;
sed -i  -e 's/&lt;\/command&gt;/<\/command>/g' en-US/Tapset_Reference_Guide.xml;