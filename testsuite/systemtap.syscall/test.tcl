set dir ""
set current_dir ""

proc cleanup {} {
    global dir current_dir
    if {$current_dir != ""}  {
	cd $current_dir
	set current_dir ""
    }
    if {$dir != ""} {
#	puts "rm -rf $dir"
	exec rm -rf $dir
	set dir ""
    }
}

proc cleanup_and_exit {} {
#    puts "cleanup_and_exit"
    cleanup
    exit 0
}

proc bgerror {error} {
    puts "ERROR: $error"
    cleanup
}
trap {cleanup_and_exit} SIGINT

proc run_one_test {filename flags} {
    global dir current_dir

    set testname [file tail [string range $filename 0 end-2]]
    set result "UNSUPP"

    if {[catch {exec mktemp -d [pwd]/staptestXXXXX} dir]} {
	puts stderr "Failed to create temporary directory: $dir"
	cleanup
    }

    target_compile $filename $dir/$testname executable $flags
    
    set sys_prog "[file dirname [file normalize $filename]]/sys.stp"
    set cmd "stap --skip-badvars -c $dir/${testname} ${sys_prog}"
    
    # Extract the expected results
    # Use the preprocessor so we can ifdef tests in and out
    
    set ccmd "gcc -E -C -P $filename"
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
	    regsub -all {\+} $line {\\+} line
	    regsub -all {\*} $line {\\*} line
	    
	    regsub -all NNNN $line {[\-0-9]+} line
	    regsub -all XXXX $line {[x0-9a-fA-F]+} line
	    
	    set results($ind) $line
	    incr ind
	}
    }

    if {$ind == 0} {
	# unsupported
	cleanup
	return $result
    }

    set current_dir [pwd]
    cd $dir 
    
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
	set result "PASS"
	# puts "PASS $testname"
    } else {
	set result "FAIL $testname"
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
    }
    cleanup
    return $result
}
