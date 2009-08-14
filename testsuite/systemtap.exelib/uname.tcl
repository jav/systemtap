set ::result_string {exe: main=main
exe: main_func=main_func
exe: main_func=main_func
exe: main_func=main_func
lib: lib_main=lib_main
lib: lib_func=lib_func
lib: lib_func=lib_func
lib: lib_func=lib_func}

# Only run on make installcheck
if {! [installtest_p]} { untested "uname-$testname"; return }
if {! [uprobes_p]} { untested "uname-$testname"; return }
stap_run3 uname-$testname $srcdir/$subdir/uname.stp $testexe $testlib -c $testexe
