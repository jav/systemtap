set ::result_string {main
main_func
main_func
main_func
lib_main
lib_func
lib_func
lib_func}

# Only run on make installcheck
if {! [installtest_p]} { untested "lib-$testname"; return }
if {! [utrace_p]} { untested "lib-$testname"; return }
stap_run3 lib-$testname $srcdir/$subdir/lib.stp $testexe $testlib -c $testexe
