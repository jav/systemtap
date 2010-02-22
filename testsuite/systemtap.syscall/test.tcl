set syscall_dir ""
set current_dir ""

proc syscall_cleanup {} {
    global syscall_dir current_dir
    if {$current_dir != ""}  {
	cd $current_dir
	set current_dir ""
    }
    if {$syscall_dir != ""} {
#	puts "rm -rf $syscall_dir"
	exec rm -rf $syscall_dir
	set syscall_dir ""
    }
}

proc syscall_cleanup_and_exit {} {
#    puts "syscall_cleanup_and_exit"
    syscall_cleanup
    exit 0
}

proc bgerror {error} {
    puts "ERROR: $error"
    syscall_cleanup
}
trap {syscall_cleanup_and_exit} SIGINT

proc run_one_test {filename flags bits} {
    global syscall_dir current_dir test_script

    set testname [file tail [string range $filename 0 end-2]]

    if {[catch {exec mktemp -d [pwd]/staptestXXXXXX} syscall_dir]} {
	puts stderr "Failed to create temporary directory: $syscall_dir"
	syscall_cleanup
    }

    set res [target_compile $filename $syscall_dir/$testname executable $flags]
    if { $res != "" } {
      send_log "$bits-bit $testname : no corresponding devel environment found\n"
      untested "$bits-bit $testname"
      return
    }

    set sys_prog "[file dirname [file normalize $filename]]/${test_script}"
    set cmd "stap --skip-badvars -c $syscall_dir/${testname} ${sys_prog}"
    
    # Extract additional C flags needed to compile
    set add_flags ""
    foreach i $flags {
	if [regexp "^additional_flags=" $i] {
	    regsub "^additional_flags=" $i "" tmp
	    append add_flags " $tmp"
	}
    }

    # Extract the expected results
    # Use the preprocessor so we can ifdef tests in and out
    
    set ccmd "gcc -E -C -P $add_flags $filename"
    # XXX: but note, this will expand all system headers too!
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
	# unsupported
	syscall_cleanup
	unsupported "$bits-bit $testname not supported on this arch"
	return
    }

    set current_dir [pwd]
    cd $syscall_dir
    
    catch {eval exec $cmd} output
    
    set i 0
    foreach line [split $output "\n"] {
        # send_log "Comparing $results($i) against $line"
	if {[regexp $results($i) $line]} {
	    incr i
	    if {$i >= $ind} {break}
	}
    }
    if {$i >= $ind} {
	# puts "PASS $testname"
	pass "$bits-bit $testname"
    } else {
	send_log "$testname FAILED. output of \"$cmd\" was:"
	send_log "\n------------------------------------------\n"
	send_log $output
	send_log "\n------------------------------------------\n"
	send_log "RESULTS: (\'*\' = MATCHED EXPECTED)\n"
	set i 0
	foreach line [split $output "\n"] {
	    if {[regexp "${testname}: " $line]} {
		if {[regexp $results($i) $line]} {
		    send_log "*$line\n"
		    incr i
		    if {$i >= $ind} {break}
		} else {
		    send_log "$line\n"
		}
	    }
	}
	if {$i < $ind} {
	    send_log -- "--------- EXPECTED and NOT MATCHED ----------\n"
	}
	for {} {$i < $ind} {incr i} {
	    send_log "$results($i)\n"
	}
	fail "$bits-bit $testname"
    }
    syscall_cleanup
    return
}
