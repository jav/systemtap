#!/usr/bin/env tclsh
#
# stap_merge.tcl - systemtap merge program
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Copyright (C) Red Hat Inc, 2007
#
#

proc usage {} {
	puts stderr "$::argv0 \[-v\] \[-o output_filename\] input_files ...\n"
	exit 1
}

set outfile "stdout"
set verbose 0
set index 0
while {[string match -* [lindex $argv $index]]} {
    switch -glob -- [lindex $argv $index] {
	-v {set verbose 1}
	-o   {incr index; set outfile [lindex $argv $index]}
	default {usage}
    }
    incr index
}

if {$tcl_platform(byteOrder) == "littleEndian"} {
    set int_format i
} else {
    set int_format I
}

set files [lrange $argv $index end]

set n 0
foreach file $files {
    if {[catch {open $file} fd($n)]} {
	puts stderr $fd($n)
	exit 1
    }
    fconfigure $fd($n) -translation binary
    if {![binary scan [read $fd($n) 4] $int_format timestamp($n)]} {
	continue
    }
    set timestamp($n) [expr $timestamp($n) & 0xFFFFFFFF]
    incr n
}
set ncpus $n

if {$outfile != "stdout"} {
    if {[catch {open $outfile w} outfile]} {
	puts stderr $outfile
	exit 1
    }
}
fconfigure $outfile -translation binary

while {1} {
    set mincpu -1
    for {set n 0} {$n < $ncpus} {incr n} {
	if {[info exists fd($n)] && (![info exists min] || $timestamp($n) <= $min)} {
	    set min $timestamp($n)
	    set mincpu $n
	}
    }

    if {![info exists min]} {break}

    if {![binary scan [read $fd($mincpu) 4] $int_format len]} {
	puts stderr "Error reading length from channel $mincpu"
	exit 1
    }

    if {$verbose == 1} {
	puts stderr "\[CPU:$mincpu, seq=$min, length=$len\]"
    }

    set data [read $fd($mincpu) $len]
    puts -nonewline $outfile $data

    set data [read $fd($mincpu) 4]
    if {$data == ""} {
	unset fd($mincpu)
    } else {
	binary scan $data $int_format timestamp($mincpu)
	set timestamp($mincpu) [expr $timestamp($mincpu) & 0xFFFFFFFF]
    }
    unset min
}
