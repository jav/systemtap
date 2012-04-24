#!/usr/bin/env tclsh
package require Expect

#####################
#
# This script tests a single syscall test executable against the
# syscall tapset.  To use, do the following:
#
#   # gcc -o TESTNAME TESTNAME.c
#   # test-debug-cmd.tcl TESTNAME
#
# The script output will appear on stdout.
#
#####################

set syscall_dir ""
set current_dir ""

proc syscall_cleanup {} {
    global syscall_dir current_dir
    if {$current_dir != ""} {
        cd $current_dir
	if {$syscall_dir != ""} {exec rm -rf $syscall_dir}
	set current_dir ""
    }
    exit 0
}

proc usage {progname} {
    puts "Usage: $progname testname [C filename]"
    syscall_cleanup
}

proc bgerror {error} {
    puts "ERROR: $error"
    syscall_cleanup
}
trap {syscall_cleanup} SIGINT
set testname [lindex $argv 0]
if {$testname == ""} {
    usage $argv0
    exit
}

set filename [lindex $argv 1]
if {$filename == ""} {
    set filename "${testname}.c"
    set sys_prog "../sys.stp"
} else {
    set sys_prog "[file dirname [file normalize $filename]]/sys.stp"
}
set cmd "stap -c ../${testname} ${sys_prog}"

# extract the expected results
# Use the preprocessor so we can ifdef tests in and out

set ccmd "gcc -E -C -P $filename"
catch {eval exec $ccmd} output

set ind 0
foreach line [split $output "\n"] {
  if {[regsub {//staptest//} $line {} line]} {
    set line "$testname: [string trimleft $line]"

    # We need to quote all these metacharacters
    regsub -all {\(} $line {\\(} line
    regsub -all {\)} $line {\\)} line
    regsub -all {\|} $line {\|} line
    # + and * are metacharacters, but should always be used
    # as metacharacters in the expressions, don't escape them.
    #regsub -all {\+} $line {\\+} line
    #regsub -all {\*} $line {\\*} line

    # Turn '[[[[' and ']]]]' into '(' and ')' respectively.
    # Because normally parens get quoted, this allows us to
    # have non-quoted parens.
    regsub -all {\[\[\[\[} $line {(} line
    regsub -all {\]\]\]\]} $line {)} line

    regsub -all NNNN $line {[\-0-9]+} line
    regsub -all XXXX $line {[x0-9a-fA-F]+} line

    set results($ind) $line
    incr ind
  }
}

if {$ind == 0} {
    puts "UNSUPP"
    syscall_cleanup
    exit
}

if {[catch {exec mktemp -d staptestXXXXXX} syscall_dir]} {
    puts stderr "Failed to create temporary directory: $syscall_dir"
    syscall_cleanup
}

set current_dir [pwd]
cd $syscall_dir
catch {eval exec $cmd} output

set i 0
foreach line [split $output "\n"] {
    if {[regexp $results($i) $line]} {
	incr i
	if {$i >= $ind} {break}
    }
}
if {$i >= $ind} {
    puts "PASS"
} else {
    puts "FAIL"
}

puts "RESULTS: (\'*\' = MATCHED EXPECTED)\n"
set i 0
foreach line [split $output "\n"] {
    if {[regexp "${testname}: " $line]} {
	if {[regexp $results($i) $line]} {
	    puts "*$line"
	    incr i
	    if {$i >= $ind} {break}
	} else {
	    puts "$line"
	}
    }
}
if {$i < $ind} {
    puts "--------- EXPECTED and NOT MATCHED ----------\n"
}
for {} {$i < $ind} {incr i} {
    puts "$results($i)"
}

