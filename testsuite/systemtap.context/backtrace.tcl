set m1 0
set m2 0
set m3 0
set m4 0
set m5 0
set m6 0

spawn stap backtrace.stp
#exp_internal 1
expect {
    -timeout 240
    "Systemtap probe: begin\r\n" {
	pass "backtrace of begin probe"
	exec echo 0 > /proc/stap_test_cmd
	exp_continue
    }
    -re {^backtrace from module\(\"systemtap_test_module2\"\)\.function\(\"yyy_func3@[^\r\n]+\r\n} {
	incr m1
	expect {
	    -timeout 5
	    -re {^ 0x[a-f0-9]+ : yyy_func3[^\[]+\[systemtap_test_module2\]\r\n} {
		if {$m1 == 1} {incr m1}
		exp_continue
	    }
	    -re {^ 0x[a-f0-9]+ : yyy_func2[^\[]+\[systemtap_test_module2\]\r\n} {
		if {$m1 == 2} {incr m1}
		exp_continue
	    }
	    -re {^ 0x[a-f0-9]+ : yyy_func1[^\[]+\[systemtap_test_module2\]\r\n} {
		if {$m1 == 3} {incr m1}
	    }
	}
	exp_continue
    }
    -re {.*---\r\nthe call stack is 0x[a-f0-9]+ [^\r\n]+\r\n} {
	incr m2
	expect {
	    -timeout 5
	    -re {.*---\r\n 0x[a-f0-9]+ : yyy_func3[^\[]+\[systemtap_test_module2\]\r\n} {
		if {$m2 == 1} {incr m2}
		exp_continue
	    }
	    -re {^ 0x[a-f0-9]+ : yyy_func2[^\[]+\[systemtap_test_module2\]\r\n} {
		if {$m2 == 2} {incr m2}
		exp_continue
	    }
	    -re {^ 0x[a-f0-9]+ : yyy_func1[^\[]+\[systemtap_test_module2\]\r\n} {
		if {$m2 == 3} {incr m2}
	    }
	}
	exp_continue
    }
    -re {.*backtrace from module\(\"systemtap_test_module2\"\)\.function\(\"yyy_func4@[^\r\n]+\r\n} {
	incr m3
	expect {
	    -timeout 5
            -re {^Returning from: 0x[a-f0-9]+ : yyy_func4[^\[]+\[systemtap_test_module2\]\r\n} {
		if {$m3 == 1} {incr m3}
		exp_continue
            }
            -re {^Returning to  : 0x[a-f0-9]+ : yyy_func3[^\[]+\[systemtap_test_module2\]\r\n} {
		if {$m3 == 2} {incr m3}
		exp_continue
            }
	    -re {^ 0x[a-f0-9]+ : yyy_func2[^\[]+\[systemtap_test_module2\]\r\n} {
		if {$m3 == 3} {incr m3}
		exp_continue
	    }
	    -re {^ 0x[a-f0-9]+ : yyy_func1[^\[]+\[systemtap_test_module2\]\r\n} {
		if {$m3 == 4} {incr m3}
	    }
	}
	exp_continue
    }
    -re {.*---\r\nthe return stack is 0x[a-f0-9]+ [^\r\n]+\r\n} {
	incr m4
	expect {
	    -timeout 5
	    -re {.*0x[a-f0-9]+ : kretprobe_trampoline_holder[^\[]+\[\]\r\n} {
		if {$m4 == 1} {incr m4}
		exp_continue
	    }
	    -re {^ 0x[a-f0-9]+ : yyy_func2[^\[]+\[systemtap_test_module2\]\r\n} {
		if {$m4 == 2} {incr m4}
		exp_continue
	    }
	    -re {^ 0x[a-f0-9]+ : yyy_func1[^\[]+\[systemtap_test_module2\]\r\n} {
		if {$m4 == 3} {incr m4}
	    }
	}
	exp_continue
    }
    -re {.*backtrace from timer.profile:\r\n} {
	incr m5
	expect {
	    -timeout 5
	    -re {^ 0x[a-f0-9]+[^\r\n]+\r\n} {
		if {$m5 == 1} {incr m5}
	    }
	}
	exp_continue
    }
    -re {.*---\r\nthe profile stack is 0x[a-f0-9]+[^\r\n]+\r\n} {
	incr m6
	expect {
	    -timeout 5
	    -re {.*---\r\n 0x[a-f0-9]+[^\r\n]+\r\n} {
		if {$m6 == 1} {incr m6}
	    }
	}
    }
   eof {fail "backtrace of yyy_func3, yyy_func4.return and timer.profile. unexpected EOF" }
}
send "\003"
if {$m1 == 4} {
    pass "backtrace of yyy_func3"
} else {
    fail "backtrace of yyy_func3 ($m1)"
}
if {$m2 == 4} {
    pass "print_stack of yyy_func3"
} else {
    fail "print_stack of yyy_func3 ($m2)"
}
if {$m3 == 5} {
    pass "backtrace of yyy_func4.return"
} else {
    fail "backtrace of yyy_func4.return ($m3)"
}
if {$m4 == 4} {
    pass "print_stack of yyy_func4.return"
} else {
    fail "print_stack of yyy_func4.return ($m4)"
}
if {$m5 == 2} {
    pass "backtrace of timer.profile"
} else {
    fail "backtrace of timer.profile ($m5)"
}
if {$m6 == 2} {
    pass "print_stack of timer.profile"
} else {
    fail "print_stack of timer.profile ($m6)"
}

close
wait
