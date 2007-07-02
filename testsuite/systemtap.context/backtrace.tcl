set m1 0
set m2 0

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
    -re {.*---\r\nthe stack is 0x[a-f0-9]+ [^\r\n]+\r\n} {
	incr m2
	expect {
	    -timeout 5
	    -re {.*0x[a-f0-9]+ : yyy_func3[^\[]+\[systemtap_test_module2\]\r\n} {
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
    }
    eof {fail "backtrace of yyy_func3. unexpected EOF" }
}
send "\003"
if {$m1 == 4} {
    pass "backtrace of yyy_func3"
} else {
    fail "backtrace of yyy_func3"
}
if {$m2 == 4} {
    pass "print_stack of yyy_func3"
} else {
    fail "print_stack of yyy_func3 ($m2)"
}

close
wait
