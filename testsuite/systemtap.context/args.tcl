spawn stap $srcdir/$subdir/args.stp
expect {
    -timeout 240
    "READY" {
	exec echo 1 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_int -1 200 300\r\nyyy_int returns 499\r\n" {
		pass "integer function arguments"
	    }
	    timeout {fail "integer function arguments"}
	}
	exec echo 2 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_uint 4294967295 200 300\r\nyyy_uint returns 499\r\n" {
		pass "unsigned function arguments"
	    }
	    timeout {fail "unsigned function arguments"}
	}
	exec echo 3 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_long -1 200 300\r\nyyy_long returns 499\r\n" {
		pass "long function arguments"
	    }
	    timeout {fail "long function arguments"}
	}
	exec echo 4 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_int64 -1 200 300\r\nyyy_int64 returns 499\r\n" {
		pass "int64 function arguments"
	    }
	    timeout {fail "int64 function arguments"}
	}
	exec echo 5 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_char a b c\r\nyyy_char returns Q\r\n" {
		pass "char function arguments"
	    }
	    timeout {fail "char function arguments"}
	}
	exec echo 6 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_str Hello-System-Tap\r\nyyy_str returns XYZZY\r\n" {
		pass "string function arguments"
	    }
	    timeout {fail "string function arguments"}
	}
    }
    eof {fail "function arguments: unexpected timeout"}
}
exec kill -INT -[exp_pid]
closewait
