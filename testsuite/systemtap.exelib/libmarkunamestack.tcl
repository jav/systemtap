# Only run on make installcheck
if {! [installtest_p]} { untested "libmarkunamestack-$testname"; return }
if {! [uprobes_p]} { untested "libmarkunamestack-$testname"; return }

# Big jumbo test, for tracking down bugs, use the individual tests.

# Output for ustack part is:
#print_ubacktrace exe 0
# 0x080484ba : main_func+0xa/0x29 [.../uprobes_exe]
# 0x080484f6 : main+0x1d/0x37 [.../uprobes_exe]
#print_ustack exe 1
# 0x080484ba : main_func+0xa/0x29 [.../uprobes_exe]
# 0x080484c9 : main_func+0x19/0x29 [.../uprobes_exe]
# 0x080484f6 : main+0x1d/0x37 [.../uprobes_exe]
#print_ubacktrace lib 2
# 0x00db2422 : lib_func+0x16/0x2b [.../libuprobes_lib.so]
# 0x00db2455 : lib_main+0x1e/0x29 [.../libuprobes_lib.so]
# 0x080484d0 : main_func+0x20/0x29 [.../uprobes_exe]
# 0x080484c9 : main_func+0x19/0x29 [.../uprobes_exe]
# 0x080484c9 : main_func+0x19/0x29 [.../uprobes_exe]
# 0x080484f6 : main+0x1d/0x37 [.../uprobes_exe]
#print_ustack lib 3
# 0x00db2422 : lib_func+0x16/0x2b [.../libuprobes_lib.so]
# 0x00db2431 : lib_func+0x25/0x2b [.../libuprobes_lib.so]
# 0x00db2455 : lib_main+0x1e/0x29 [.../libuprobes_lib.so]
# 0x080484d0 : main_func+0x20/0x29 [.../uprobes_exe]
# 0x080484c9 : main_func+0x19/0x29 [.../uprobes_exe]
# 0x080484c9 : main_func+0x19/0x29 [.../uprobes_exe]
# 0x080484f6 : main+0x1d/0x37 [.../uprobes_exe]

set lib 0
set mark 0
set uname 0

set print 0
set main 0
set main_func 0
set lib_main 0
set lib_func 0
send_log "Running: stap $srcdir/$subdir/libmarkunamestack.stp $testexe $testlib -c $testexe\n"
spawn stap $srcdir/$subdir/libmarkunamestack.stp $testexe $testlib -c $testexe

wait -i $spawn_id
expect {
  -timeout 60
    -re {^print_[^\r\n]+\r\n} {incr print; exp_continue}

# lib
    -re {^main\r\n} {incr lib; exp_continue}
    -re {^main_func\r\n} {incr lib; exp_continue}
    -re {^lib_main\r\n} {incr lib; exp_continue}
    -re {^lib_func\r\n} {incr lib; exp_continue}

# mark
    -re {^main_count: [1-3]\r\n} {incr mark; exp_continue}
    -re {^func_count: [1-3]\r\n} {incr mark; exp_continue}

# uname
    -re {^exe: main=main\r\n} {incr uname; exp_continue}
    -re {^exe: main_func=main_func\r\n} {incr uname; exp_continue}
    -re {^lib: lib_main=lib_main\r\n} {incr uname; exp_continue}
    -re {^lib: lib_func=lib_func\r\n} {incr uname; exp_continue}

# ustack
    -re {^ 0x[a-f0-9]+ : main\+0x[^\r\n]+\r\n} {incr main; exp_continue}
    -re {^ 0x[a-f0-9]+ : main_func\+0x[^\r\n]+\r\n} {incr main_func; exp_continue}
    -re {^ 0x[a-f0-9]+ : lib_main\+0x[^\r\n]+\r\n} {incr lib_main; exp_continue}
    -re {^ 0x[a-f0-9]+ : lib_func\+0x[^\r\n]+\r\n} {incr lib_func; exp_continue}

    timeout { fail "libmarkunamestack-$testname (timeout)" }
    eof { }
}

if {$lib == 8} { pass "lib-$testname" } {
    fail "lib-$testname ($lib)"
}

if {$mark == 6} { pass "mark-$testname" } {
    fail "mark-$testname ($mark)"
}

if {$uname == 8} { pass "uname-$testname" } {
    fail "uname-$testname ($uname)"
}

if {$print == 4} { pass "ustack-$testname print" } {
    fail "ustack-$testname print ($print)"
}

if {$main == 4} { pass "ustack-$testname main" } {
    fail "ustack-$testname main ($main)"
}

if {$main_func == 9} { pass "ustack-$testname main_func" } {
    fail "ustack-$testname main_func ($main_func)"
}

if {$lib_main == 2} { pass "ustack-$testname lib_main" } {
    fail "ustack-$testname lib_main ($lib_main)"
}

if {$lib_func == 3} { pass "ustack-$testname lib_func" } {
    fail "ustack-$testname lib_func ($lib_func)"
}
