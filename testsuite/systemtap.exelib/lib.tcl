set ::result_string {main
main_func 3
main_func 2
main_func 1
lib_main
lib_func 3
lib_func 2
lib_func 1}

# Only run on make installcheck
if {! [installtest_p]} { untested "lib-$testname"; return }
if {! [uprobes_p]} { untested "lib-$testname"; return }
stap_run3 lib-$testname $srcdir/$subdir/lib.stp $testexe $testlib -c $testexe
