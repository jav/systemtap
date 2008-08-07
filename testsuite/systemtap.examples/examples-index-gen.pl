# Generates index files from examples .meta file info.
# Copyright (C) 2008 Red Hat Inc.
#
# This file is part of systemtap, and is free software.  You can
# redistribute it and/or modify it under the terms of the GNU General
# Public License (GPL); either version 2, or (at your option) any
# later version.

use strict;
use warnings;

use File::Copy;
use File::Find;
use File::Path;
use Text::Wrap;

my $inputdir;
if ($#ARGV >= 0) {
    $inputdir = $ARGV[0];
} else {
    $inputdir = ".";
}

my $outputdir;
if ($#ARGV >= 1) {
    $outputdir = $ARGV[1];
} else {
    $outputdir = $inputdir;
}

my %scripts = ();
print "Parsing .meta files in $inputdir...\n";
find(\&parse_meta_files, $inputdir);

my $meta;
my $subsystem;
my %subsystems;
my $keyword;
my %keywords;

# Adds a formatted meta entry to a given file handle as text.
sub add_meta_txt(*;$) {
    my($file,$meta) = @_;

    print $file "$scripts{$meta}{name} - $scripts{$meta}{title}\n";

    print $file "output: $scripts{$meta}{output}, ";
    print $file "exits: $scripts{$meta}{exit}, ";
    print $file "status: $scripts{$meta}{status}\n";

    print $file "subsystem: $scripts{$meta}{subsystem}, ";
    print $file "keywords: $scripts{$meta}{keywords}\n\n";

    $Text::Wrap::columns = 72;
    my $description = wrap('  ', '  ', $scripts{$meta}{description});
    print $file "$description\n\n\n";
}

# Adds a formatted meta entry to a given file handle as text.
sub add_meta_html(*;$) {
    my($file,$meta) = @_;

    my $name = $scripts{$meta}{name};
    print $file "<li><a href=\"$name\">$name</a> ";
    print $file "- $scripts{$meta}{title}<br>\n";

    print $file "output: $scripts{$meta}{output}, ";
    print $file "exits: $scripts{$meta}{exit}, ";
    print $file "status: $scripts{$meta}{status}<br>\n";

    print $file "subsystem: $scripts{$meta}{subsystem}, ";
    print $file "keywords: $scripts{$meta}{keywords}<br>\n";

    print $file "<p>$scripts{$meta}{description}";
    print $file "</p></li>\n";
}

my $HEADER = "SYSTEMTAP EXAMPLES INDEX\n"
    . "(see also subsystem-index.txt, keyword-index.txt)\n\n";

my $header_tmpl = "$inputdir/html_header.tmpl";
open(TEMPLATE, "<$header_tmpl")
    || die "couldn't open $header_tmpl, $!";
my $HTMLHEADER = do { local $/;  <TEMPLATE> };
close(TEMPLATE);
my $footer_tmpl = "$inputdir/html_footer.tmpl";
open(TEMPLATE, "<$footer_tmpl")
    || die "couldn't open $footer_tmpl, $!";
my $HTMLFOOTER = do { local $/;  <TEMPLATE> };
close(TEMPLATE);

# Output full index and collect subsystems and keywords
my $fullindex = "$outputdir/index.txt";
open (FULLINDEX, ">$fullindex")
    || die "couldn't open $fullindex: $!";
print "Creating $fullindex...\n";
print FULLINDEX $HEADER;

my $fullhtml = "$outputdir/index.html";
open (FULLHTML, ">$fullhtml")
    || die "couldn't open $fullhtml: $!";
print "Creating $fullhtml...\n";
print FULLHTML $HTMLHEADER;
print FULLHTML "<h2>All Examples</h2>\n";
print FULLHTML "<ul>\n";

foreach $meta (sort keys %scripts) {

    add_meta_txt(\*FULLINDEX, $meta);
    add_meta_html(\*FULLHTML, $meta);

    # Collect subsystems
    foreach $subsystem (split(/ /, $scripts{$meta}{subsystem})) {
	if (defined $subsystems{$subsystem}) {
	    push(@{$subsystems{$subsystem}}, $meta);
	} else {
	    $subsystems{$subsystem} = [ $meta ];
	}
    }

    # Collect keywords
    foreach $keyword (split(/ /, $scripts{$meta}{keywords})) {
	if (defined $keywords{$keyword}) {
	    push(@{$keywords{$keyword}}, $meta);
	} else {
	    $keywords{$keyword} = [ $meta ];
	}
    }
}
print FULLHTML "</ul>\n";
print FULLHTML $HTMLFOOTER;
close (FULLINDEX);
close (FULLHTML);

my $SUBHEADER = "SYSTEMTAP EXAMPLES INDEX BY SUBSYSTEM\n"
    . "(see also index.txt, keyword-index.txt)\n\n";

# Output subsystem index
my $subindex = "$outputdir/subsystem-index.txt";
open (SUBINDEX, ">$subindex")
    || die "couldn't open $subindex: $!";
print "Creating $subindex...\n";
print SUBINDEX $SUBHEADER;

my $subhtml = "$outputdir/subsystem-index.html";
open (SUBHTML, ">$subhtml")
    || die "couldn't open $subhtml: $!";
print "Creating $subhtml...\n";
print SUBHTML $HTMLHEADER;
print SUBHTML "<h2>Examples by Subsystem</h2>\n";

foreach $subsystem (sort keys %subsystems) {
    print SUBINDEX "= " . (uc $subsystem) . " =\n\n";
    print SUBHTML "<h3>" . (uc $subsystem) . "</h3>\n";
    print SUBHTML "<ul>\n";

    foreach $meta (sort @{$subsystems{$subsystem}}) {
	add_meta_txt(\*SUBINDEX,$meta);
	add_meta_html(\*SUBHTML,$meta);
    }
    print SUBHTML "</ul>\n";
}
print SUBHTML $HTMLFOOTER;
close (SUBINDEX);
close (SUBHTML);

my $KEYHEADER = "SYSTEMTAP EXAMPLES INDEX BY KEYWORD\n"
    . "(see also index.txt, subsystem-index.txt)\n\n";

# Output subsystem index
my $keyindex = "$outputdir/keyword-index.txt";
open (KEYINDEX, ">$keyindex")
    || die "couldn't open $keyindex: $!";
print "Creating $keyindex...\n";
print KEYINDEX $KEYHEADER;

my $keyhtml = "$outputdir/keyword-index.html";
open (KEYHTML, ">$keyhtml")
    || die "couldn't open $keyhtml: $!";
print "Creating $keyhtml...\n";
print KEYHTML $HTMLHEADER;
print KEYHTML "<h2>Examples by Keyword</h2>\n";

foreach $keyword (sort keys %keywords) {
    print KEYINDEX "= " . (uc $keyword) . " =\n\n";
    print KEYHTML "<h3>" . (uc $keyword) . "</h3>\n";
    print KEYHTML "<ul>\n";

    foreach $meta (sort @{$keywords{$keyword}}) {
	add_meta_txt(\*KEYINDEX,$meta);
	add_meta_html(\*KEYHTML,$meta);
    }
    print KEYHTML "</ul>\n";
}
print KEYHTML $HTMLFOOTER;
close (KEYINDEX);
close (KEYHTML);

my @supportfiles
    = ("systemtapcorner.gif",
       "systemtap.css",
       "systemtaplogo.png");
if ($inputdir ne $outputdir) {
    my $file;
    print "Copying support files...\n";
    foreach $file (@supportfiles) {
	my $orig = "$inputdir/$file";
	my $dest = "$outputdir/$file";
	print "Copying $file to $dest...\n";
	copy("$orig", $dest) or die "$file cannot be copied to $dest, $!";
    }
}

sub parse_meta_files {
    my $file = $_;
    my $filename = $File::Find::name;

    if (-f $file && $file =~ /\.meta$/) {
	open FILE, $file or die "couldn't open '$file': $!\n";

	print "Parsing $filename...\n";
	
	my $title;
	my $name;
	my $keywords;
	my $subsystem;
	my $status;
	my $exit;
	my $output;
	my $description;
	while (<FILE>) {
	    if (/^title: (.*)/) { $title = $1; }
	    if (/^name: (.*)/) { $name = $1; }
	    if (/^keywords: (.*)/) { $keywords = $1; }
	    if (/^subsystem: (.*)/) { $subsystem = $1; }
	    if (/^status: (.*)/) { $status = $1; }
	    if (/^exit: (.*)/) { $exit = $1; }
	    if (/^output: (.*)/) { $output = $1; }
	    if (/^description: (.*)/) { $description = $1; }
	}
	close FILE;

	# Remove extra whitespace
	$keywords =~ s/^\s*//;
	$keywords =~ s/\s*$//;
	$subsystem =~ s/^\s*//;
	$subsystem =~ s/\s*$//;

	# The subdir without the inputdir prefix, nor any slashes.
	my $subdir = substr $File::Find::dir, (length $inputdir);
	$subdir =~ s/^\///;
	if ($subdir ne "") {
	    $name = "$subdir/$name";
	}

	my $script = {
	    name => $name,
	    title => $title,
	    keywords => $keywords,
	    subsystem => $subsystem,
	    status => $status,
	    exit => $exit,
	    output => $output,
	    description => $description
	};

	# chop off the search dir prefix.
	$inputdir =~ s/\/$//;
	$meta = substr $filename, (length $inputdir) + 1;
	$scripts{$meta} = $script;

	# Put .stp script in output dir if necessary and create
	# subdirs if they don't exist yet.
	if ($inputdir ne $outputdir) {
	    # The subdir without the inputdir prefix, nor any slashes.
	    my $destdir = substr $File::Find::dir, (length $inputdir);
	    $destdir =~ s/^\///;
	    if ($subdir ne "") {
		if (! -d "$outputdir/$subdir") {
		    mkpath("$outputdir/$subdir", 1, 0711);
		}
	    }
	    my $orig = substr $name, (length $subdir);
	    $orig =~ s/^\///;
	    my $dest = "$outputdir/$name";
	    print "Copying $orig to $dest...\n";
	    copy($orig, $dest) or die "$orig cannot be copied to $dest, $!";
	}
    }
}
