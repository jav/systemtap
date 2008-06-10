set arglists {{} {--kelf --ignore-dwarf}}
foreach arglist $arglists {
set tag [concat numeric $arglist]
eval spawn stap $arglist $srcdir/$subdir/num_args.stp
expect {
    -timeout 240
    "READY" {
	exec echo 1 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_int -1 200 300\r\nyyy_int returns 499\r\n" {
		pass "integer function arguments -- $tag"
	    }
	    timeout {fail "integer function arguments -- $tag"}
	}
	exec echo 2 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_uint 4294967295 200 300\r\nyyy_uint returns 499\r\n" {
		pass "unsigned function arguments -- $tag"
	    }
	    timeout {fail "unsigned function arguments -- $tag"}
	}
	exec echo 3 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_long -1 200 300\r\nyyy_long returns 499\r\n" {
		pass "long function arguments -- $tag"
	    }
	    timeout {fail "long function arguments -- $tag"}
	}
	exec echo 4 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_int64 -1 200 300\r\nyyy_int64 returns 499\r\n" {
		pass "int64 function arguments -- $tag"
	    }
	    timeout {fail "int64 function arguments -- $tag"}
	}
	exec echo 5 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_char a b c\r\nyyy_char returns Q\r\n" {
		pass "char function arguments -- $tag"
	    }
	    timeout {fail "char function arguments -- $tag"}
	}
	exec echo 6 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_str Hello-System-Tap\r\nyyy_str returns XYZZY\r\n" {
		pass "string function arguments -- $tag"
	    }
	    timeout {fail "string function arguments -- $tag"}
	}
    }
    eof {fail "function arguments -- $tag: unexpected timeout"}
}
exec kill -INT -[exp_pid]
close
wait
}
