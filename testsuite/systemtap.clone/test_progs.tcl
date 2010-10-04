proc build_test_progs {Makefile test_progs} {
    global build_dir
    global srcdir subdir
    global env

    # Create the build directory and populate it
    if {[catch {exec mktemp -d staptestXXXXXX} build_dir]} {
	verbose -log "Failed to create temporary directory: $build_dir"
	return 0
    }
    foreach f [glob $srcdir/$subdir/*.c $srcdir/$subdir/*.d] {
	exec cp $f $build_dir/
    }
    exec cp $srcdir/$subdir/$Makefile $build_dir/Makefile

    # Run make.
    set includes "-isystem$env(SYSTEMTAP_INCLUDES)"
    verbose -log "exec make -C $build_dir CFLAGS=$includes"
    set res [catch "exec make -C $build_dir CFLAGS=$includes" output]
    verbose -log "$output"

    # Check that the test progs were created
    foreach f $test_progs {
	if {![file exists $build_dir/$f]} {
	    verbose -log "Test program $f doesn't exist!"
	    return 0
	}
    }
    return 1
}

proc cleanup_test_progs {} {
    global build_dir
    catch { exec kill -INT -[exp_pid] }
    if {$build_dir != ""} {
	catch { exec rm -rf $build_dir }
    }
}
