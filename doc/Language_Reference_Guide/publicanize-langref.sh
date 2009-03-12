#!/bin/bash
#this script converts the langref.tex source for the Language Reference Guide into 
#DocBook XML. the conversion is done thru latexml, a utility that comes with dblatex-0.2.7.
#the output xml file of latexml is pretty dirty, so this script is needed to further clean it up.

#copy latex file to here
cp ../langref.tex .

#convert it to raw xml
latexml langref.tex --dest=Language_Reference_Guide.xml

#remove excess whitespace
sed -i -e 's/^\s*//g' Language_Reference_Guide.xml

sed -i -e 's/<!--\ %\*\*\*\* langref.tex Line [0-9]* \*\*\*\* -->//g' Language_Reference_Guide.xml

cat Language_Reference_Guide.xml | 
perl -p -e 'undef $/;s|<!-- %SystemTap Language Reference -->\n<\?latexml options="twoside,english" class="article"\?>\n<\?latexml package="geometry"\?>\n<\?latexml RelaxNGSchema="LaTeXML"\?>\n<\?latexml RelaxNGSchema="LaTeXML"\?>\n<document xmlns="http://dlmf.nist.gov/LaTeXML">\n<title>SystemTap Language Reference</title>|<\!DOCTYPE book PUBLIC "-//OASIS//DTD DocBook XML V4.5//EN" "http://www.oasis-open.org/docbook/xml/4.5/docbookx.dtd" [
]>\n<book>\n<xi:include href="Book_Info.xml" xmlns:xi="http://www.w3.org/2001/XInclude" />|msg' | 
perl -p -e 'undef $/;s|<para xml:id="p1a">\n<p>This document was derived from other documents contributed to the SystemTap project by employees of Red Hat, IBM and Intel.</p>\n</para>\n<para xml:id="p2">\n<p>Copyright © 2007 Red Hat Inc.\nCopyright © 2007 IBM Corp.\nCopyright © 2007 Intel Corporation.</p>\n</para>\n<para xml:id="p3">\n<p>Permission is granted to copy, distribute and/or modify this document\nunder the terms of the GNU Free Documentation License, Version 1.2\nor any later version published by the Free Software Foundation;\nwith no Invariant Sections, no Front-Cover Texts, and no Back-Cover Texts.</p>\n</para>\n<para xml:id="p4">\n<p>The GNU Free Documentation License is available from\n<ref class="url" href="http://www.gnu.org/licenses/fdl.html"><text font="typewriter">http://www.gnu.org/licenses/fdl.html</text></ref> or by writing to\nthe Free Software Foundation, Inc., 51 Franklin Street,\nFifth Floor, Boston, MA 02110-1301, USA.</p>\n</para>||msg' |
#fix up screens 
perl -p -e 'undef $/;s|<itemize>\n<item>\n<verbatim font="typewriter">|<screen>|msg' |
perl -p -e 'undef $/;s|<itemize>\n<item>\n\n<verbatim font="typewriter">|<screen>|msg' |
perl -p -e 'undef $/;s|</verbatim>\n</item>\n</itemize>|</screen>|msg' |
perl -p -e 'undef $/;s|</verbatim>\n\n</item>\n</itemize>|</screen>|msg' |
#fix up index tags
perl -p -e 'undef $/;s|<index xml:id="idx">\n<title>Index</title>\n</index>|<index/>|msg' |
#needed later, for TABLES!
perl -p -e 'undef $/;s|</text>\n</td>|</entry>|msg' > clean.xml

#further fix up headers!
perl -p -i -e 's|<\?latexml searchpaths="[^>]*>\n||g' clean.xml


#change main tags
sed -i -e 's/<\/document>/<\/book>/g' clean.xml

#more fixup for screen tags
perl -p -i -e 's|<verbatim font="[^"]*">|<screen>|g' clean.xml
perl -p -i -e 's|</verbatim>|</screen>|g' clean.xml

#clean section tags
sed -i -e 's/<section refnum="[0-9]*"/<section/g' clean.xml
sed -i -e 's/<section xml:id="[0-9S]*"/<section/g' clean.xml
sed -i -e 's/<section labels="LABEL:sec:/<section id="/g' clean.xml

#clean subsection tags
sed -i -e 's/<subsection refnum="[0-9]*.[0-9]*"/<subsection/g' clean.xml
sed -i -e 's/<subsection xml:id="[S.0-9]*"/<subsection/g' clean.xml
sed -i -e 's/<subsection labels="LABEL:sub:/<subsection id="/g' clean.xml

#clean subsubsection tags
sed -i -e 's/<subsubsection refnum="[S.0-9]*"/<subsubsection/g' clean.xml
sed -i -e 's/<subsubsection xml:id="[S.0-9]*"/<subsubsection/g' clean.xml
sed -i -e 's/<subsubsection labels="LABEL:sub:/<subsubsection id="/g' clean.xml

#change section tags to chapter, yay
sed -i -e 's/<section/<chapter/g' clean.xml
sed -i -e 's/<\/section>/<\/chapter>/g' clean.xml

#change subsection and subsubsection tags to section
sed -i -e 's/<subsection/<section/g' clean.xml
sed -i -e 's/<\/subsection>/<\/section>/g' clean.xml
sed -i -e 's/<subsubsection/<section/g' clean.xml
sed -i -e 's/<\/subsubsection>/<\/section>/g' clean.xml

#remove <para, then replace <p> with <para>
sed -i -e 's/<para xml:id="[pS.0-9]*"/<para/g' clean.xml
sed -i -e 's/<para>//g' clean.xml
sed -i -e 's/<\/para>//g' clean.xml
sed -i -e 's/<p>/<para>/g' clean.xml
sed -i -e 's/<\/p>/<\/para>/g' clean.xml

#properly convert xrefs
sed -i -e 's/<ref labelref="LABEL:sub:/<xref linkend="/g' clean.xml
sed -i -e 's/<ref labelref="LABEL:sec:/<xref linkend="/g' clean.xml

#convert indexterms
sed -i -e 's/indexmark>/indexterm>/g' clean.xml
perl -p -i -e 's/<indexphrase key="[^"]*">/<primary>/g' clean.xml
sed -i -e 's/<indexphrase>/<primary>/g' clean.xml
sed -i -e 's/<\/indexphrase>/<\/primary>/g' clean.xml

#convert <emph>s
sed -i -e 's/emph>/emphasis>/g' clean.xml

#convert itemizedlists and listitems, dependent on successful exec of "fix up screens" perl routines
sed -i -e 's/<itemize xml:id="[Ii,0-9]*">/<itemizedlist>/g' clean.xml
sed -i -e 's/<item xml:id="[Ii.0-9]*">/<listitem>/g' clean.xml
sed -i -e 's/<\/itemize>/<\/itemizedlist>/g' clean.xml
sed -i -e 's/<\/item>/<\/listitem>/g' clean.xml

#convert orderedlists and their respective listitems
perl -p -i -e 's|<enumerate xml:id="[^"]*">|<orderedlist>|g' clean.xml
perl -p -i -e 's|</enumerate>|</orderedlist>|g' clean.xml
perl -p -i -e 's|<item refnum="[^"]*" xml:id="[^"]*">|<listitem>|g' clean.xml

#TRICKY: this perl expression takes all occurences of 
# <ref class="url" href="http://sourceware.org/systemtap/wiki/HomePage"><text
# font="typewriter">http://sourceware.org/systemtap/wiki/HomePage</text></ref>
# and replaces the <text font=...</ref> string with "/>". from jfearn
# note: [^"]* means "any number of occurences of characters that are NOT quotes 
# note: () groups strings/an expression together, which can be called later as $1 when replacing
perl -p -i -e 's|(<ref class="url" href="[^"]*")><text font="typewriter">[^<]*</text></ref>|$1/>|g' clean.xml

#now, convert <ref class="url" to <ulink>s
sed -i -e 's/<ref class="url" href=/<ulink url=/g' clean.xml

#TRICKY again: convert <text font=[var]> accordingly; bold is <computeroutput>, typewriter is <command>
perl -p -i -e 's|(<text font="bold">[^<]*)</text>|$1</computeroutput>|g' clean.xml
sed -i -e 's/<text font="bold">/<computeroutput>/g' clean.xml
perl -p -i -e 's|(<text font="typewriter">[^<]*)</text>|$1</command>|g' clean.xml
sed -i -e 's/<text font="typewriter">/<command>/g' clean.xml

#weird remainders, defaulting them to command
perl -p -i -e 's|(<text font="typewriter bold">[^<]*)</text>|$1</command>|g' clean.xml
sed -i -e 's/<text font="typewriter bold">/<command>/g' clean.xml
perl -p -i -e 's|(<text font="smallcaps">[^<]*)</text>|$1</emphasis>|g' clean.xml
sed -i -e 's/<text font="smallcaps">/<emphasis>/g' clean.xml

#TABLES!
#the first expression is quite dirty, since it assumes that all tables have 3 columns. dunno yet how to 
#automagicize this, since the orig XML doesn't have any attribute that specifies columns per table 
sed -i -e 's/<tabular>/<tgroup cols="3">/g' clean.xml
sed -i -e 's/tabular>/tgroup>/g' clean.xml
perl -p -i -e 's|<table placement="[^"]*" refnum="[^"]*" xml:id="([^"]*">)|<table id="$1|g' clean.xml
sed -i -e 's/caption>/title>/g' clean.xml
sed -i -e 's/tr>/row>/g' clean.xml
perl -p -i -e 's|<td[^>]*>||g' clean.xml
sed -i -e 's/<text>/<entry>/g' clean.xml

#this is needed because some indexterms have been nested inside commands *sigh*
perl -p -i -e 's|(<command>[^<]*<indexterm><primary>[^<]*</primary></indexterm>)</text>|$1</command>|g' clean.xml
#this is needed because some closer tags for <text> are on new lines; is a dirty hack since we simply
#assume that all of them are </command>
sed -i -e 's/<\/text>/<\/command>/g' clean.xml

#clean up error tags
perl -p -i -e 's|<ERROR [^/]*/ERROR>|<!-- ERROR TAG REMOVED -->|g' clean.xml
#clean up "Math" tags (like, wtf)
perl -p -i -e 's|<Math [^>]*><XMath><XMApp>|<command>|g' clean.xml
perl -p -i -e 's|<XMTok [^>]*>||g' clean.xml
perl -p -i -e 's|</XMTok>||g' clean.xml
perl -p -i -e 's|</XMApp>||g' clean.xml
perl -p -i -e 's|</XMath>||g' clean.xml
perl -p -i -e 's|</Math>|</command>|g' clean.xml

#remove "About this guide" section
#perl -p -i -e 'undef $/;s|<section>\n<title>About this guide</title>||msg' clean.xml

#finalize: copy clean.xml to en-US, then deletes it
cp clean.xml en-US/Language_Reference_Guide.xml

#delete excess files
rm langref.tex
rm clean.xml
rm Language_Reference_Guide.xml
