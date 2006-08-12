#!/usr/bin/env expect

set dir ""
set current_dir ""

proc cleanup {} {
  global dir current_dir
  if {$current_dir != ""} {
    cd $current_dir
    if {$dir != ""} {exec rm -rf $dir}
    set current_dir ""
  }
  exit 0
}

proc usage {progname} {
    puts "Usage: $progname testname [modulename]"
}

proc bgerror {error} {
    puts "ERROR: $error"
    cleanup
}
trap {cleanup} SIGINT
set testname [lindex $argv 0]
set modname [lindex $argv 1]

if {$testname == ""} {
    usage $argv0
    exit
}
set filename "${testname}.c"

if {$modname == ""} {
    set cmd "stap -c ../${testname} ../sys.stp"
} else {
    set stpd ""
    set stpd_list "/usr/local/libexec/systemtap/stpd /usr/libexec/systemtap/stpd"
    foreach path $stpd_list {
	if {[file exists $path]} {
	    set stpd $path
	    break
	}
    }
    if {$stpd == ""} {
	puts "stpd not found!"
	exit
    }
    set user $::tcl_platform(user)
    set cmd "sudo $stpd -rmq  -u $user -c ../${testname} ${modname}"
}

# Extract the expected results
# Use the preprocessor so we can ifdef tests in and out

set ccmd "gcc -E -C -P $filename"
catch {eval exec $ccmd} output

set ind 0
foreach line [split $output "\n"] {
  if {[regsub {//} $line {} line]} {
    set line "$testname: [string trimleft $line]"

    regsub -all {\(} $line {\\(} line
    regsub -all {\)} $line {\\)} line
    regsub -all {\|} $line {\|} line

    regsub -all NNNN $line {[\-0-9]+} line
    regsub -all XXXX $line {[x0-9a-fA-F]+} line

    set results($ind) $line
    incr ind
  }
}

if {$ind == 0} {
  puts "UNSUPP"
  cleanup
  exit
}

if {[catch {exec mktemp -d staptestXXXXX} dir]} {
    puts "Failed to create temporary directory: $dir"
    cleanup
}
set current_dir [pwd]
cd $dir
 
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
    puts "$testname FAILED"
    puts "RESULTS: (\'*\' = MATCHED EXPECTED)"
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
	puts "--------- EXPECTED and NOT MATCHED ----------"
    }
    for {} {$i < $ind} {incr i} {
	puts "$results($i)"
    }
}
cleanup
