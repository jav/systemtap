set ::result_string {main_count: 3
main_count: 2
main_count: 1
func_count: 3
func_count: 2
func_count: 1}

# Only run on make installcheck
if {! [installtest_p]} { untested "lib-$testname"; return }
if {! [uprobes_p]} { untested "lib-$testname"; return }
stap_run3 mark-$testname $srcdir/$subdir/mark.stp $testexe $testlib -c $testexe
