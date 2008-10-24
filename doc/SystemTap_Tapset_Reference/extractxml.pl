#! /usr/bin/perl
# Generates xml files from tapset .stp files.
# Copyright (C) 2008 Red Hat Inc.
#
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.

use strict;
use warnings;

use Cwd 'abs_path';
use File::Copy;
use File::Find;
use File::Path;
use Text::Wrap;
use IO::File;
use POSIX qw(tmpnam);

my $XMLHEADER = 
    "<?xml version='1.0'?>\n"
    . "<!DOCTYPE chapter PUBLIC \"-//OASIS//DTD DocBook XML V4.5//EN\" \"http://www.oasis-open.org/docbook/xml/4.5/docbookx.dtd\" [\n"
    . "]>\n"
    ."<!-- This file is machine generated based on tapset files \n"
    . "   Do not modify this file -->\n"
    . "<book>\n"
    . "<xi:include href=\"Book_Info.xml\" xmlns:xi=\"http://www.w3.org/2001/XInclude\" />\n"
    . "<xi:include href=\"Preface.xml\" xmlns:xi=\"http://www.w3.org/2001/XInclude\" />\n"
    . "<xi:include href=\"Introduction.xml\" xmlns:xi=\"http://www.w3.org/2001/XInclude\" />\n"
;
my $XMLFOOTER =
    "<xi:include href=\"Revision_History.xml\" xmlns:xi=\"http://www.w3.org/2001/XInclude\" />\n"
    . "<index />\n"
    ."</book>\n";

my $XML_CHAPTER_HEADER = 
    "<?xml version='1.0'?>\n"
    . "<!DOCTYPE chapter PUBLIC \"-//OASIS//DTD DocBook XML V4.5//EN\" \"http://www.oasis-open.org/docbook/xml/4.5/docbookx.dtd\" [\n"
    . "]>\n"
    . "<!-- This file is extracted from the tapset files \n"
    . "   Do not modify this file -->\n";

my $XML_CHAPTER_FOOTER = "";

my $inputdir;
if ($#ARGV >= 0) {
    $inputdir = $ARGV[0];
} else {
    $inputdir = ".";
}
$inputdir = abs_path($inputdir);

my $outputdir;
if ($#ARGV >= 1) {
    $outputdir = $ARGV[1];
} else {
    $outputdir = $inputdir;
}
$outputdir = abs_path($outputdir);

#attempt to create the output directory
if ($inputdir ne $outputdir) {
    if (! -d "$outputdir") {
	mkpath("$outputdir", 1, 0711);
    }
}

my %scripts = ();

print "Extracting xml from .stp files in $inputdir...\n";
find(\&extract_xml, $inputdir);


# Output list of extracted xml files
my $tapsetxml = "$outputdir/Tapset_Reference.xml";
open (TAPSETXML, ">$tapsetxml")
    || die "couldn't open $tapsetxml: $!";
print "Creating $tapsetxml...\n";
print TAPSETXML $XMLHEADER;

my $tapset;
foreach $tapset (sort keys %scripts) {
    print TAPSETXML 	"<xi:include href=\"$tapset\" xmlns:xi=\"http://www.w3.org/2001/XInclude\" />\n"

}
print TAPSETXML $XMLFOOTER;
close (TAPSETXML);


sub extract_xml {
    my $file = $_;
    my $filename = $File::Find::name;
    my $ofile;
    my $ofilefullt;
    my $ofilefull;

    if (-f $file && $file =~ /\.stp$/) {
	open FILE, $file or die "couldn't open '$file': $!\n";

	$ofilefullt = tmpnam();
	open OFILET, ">$ofilefullt" or die "couldn't open '$ofilefullt': $!\n";

	print "Extracting xml from $filename...\n";
	
	while (<FILE>) {
	    print OFILET if s/\s*\/\/\///;
	}
	close OFILET;
	close FILE;

	#If xml was extracted make a .xml file
	if (-s $ofilefullt) {
	    #get rid of the inputdir part and .stp, add .xml
	    # chop off the search dir prefix.
	    $inputdir =~ s/\/$//;
	    $ofile = substr $filename, (length $inputdir) + 1;
	    $ofile =~ s/.stp/.xml/;
	    $ofile =~ s/\//_/g;
	    $scripts{$ofile} = $ofile;
	    print "$ofile\n";
	    $ofilefull = "$outputdir/$ofile";
	    open OFILE, ">$ofilefull"
		or die "couldn't open '$ofilefull': $!\n";
	    open OFILET, "$ofilefullt"
		or die "couldn't open '$ofilefullt': $!\n";
	    print OFILE "$XML_CHAPTER_HEADER";
	    while (<OFILET>) {
		print OFILE ;
	    }
	    print OFILE "$XML_CHAPTER_FOOTER";
	    close OFILET;
	    close OFILE;
	}
	unlink($ofilefullt);
    }
}
