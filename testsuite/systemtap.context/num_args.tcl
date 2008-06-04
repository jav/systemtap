spawn stap $srcdir/$subdir/num_args.stp
expect {
    -timeout 240
    "READY" {
	exec echo 1 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_int -1 200 300\r\nyyy_int returns 499\r\n" {
		pass "integer function arguments (numeric)"
	    }
	    timeout {fail "integer function arguments (numeric)"}
	}
	exec echo 2 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_uint 4294967295 200 300\r\nyyy_uint returns 499\r\n" {
		pass "unsigned function arguments (numeric)"
	    }
	    timeout {fail "unsigned function arguments (numeric)"}
	}
	exec echo 3 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_long -1 200 300\r\nyyy_long returns 499\r\n" {
		pass "long function arguments (numeric)"
	    }
	    timeout {fail "long function arguments (numeric)"}
	}
	exec echo 4 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_int64 -1 200 300\r\nyyy_int64 returns 499\r\n" {
		pass "int64 function arguments (numeric)"
	    }
	    timeout {fail "int64 function arguments (numeric)"}
	}
	exec echo 5 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_char a b c\r\nyyy_char returns Q\r\n" {
		pass "char function arguments (numeric)"
	    }
	    timeout {fail "char function arguments (numeric)"}
	}
	exec echo 6 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_str Hello-System-Tap\r\nyyy_str returns XYZZY\r\n" {
		pass "string function arguments (numeric)"
	    }
	    timeout {fail "string function arguments (numeric)"}
	}
    }
    eof {fail "function arguments (numeric): unexpected timeout"}
}
exec kill -INT -[exp_pid]
close
wait
