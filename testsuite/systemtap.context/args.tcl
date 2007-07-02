spawn stap args.stp
expect {
    -timeout 240
    "READY" {
	puts "READY"
	exec echo 1 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_int -1 200 300\r\nyyy_int returns 499\r\n" {
		pass "integer function arguments"
	    }
	}
	exec echo 2 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_uint 4294967295 200 300\r\nyyy_uint returns 499\r\n" {
		pass "unsigned function arguments"
	    }
	}
	exec echo 3 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_long -1 200 300\r\nyyy_long returns 499\r\n" {
		pass "long function arguments"
	    }
	}
	exec echo 4 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_int64 -1 200 300\r\nyyy_int64 returns 499\r\n" {
		pass "int64 function arguments"
	    }
	}
	exec echo 5 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_char 97 98 99\r\nyyy_char returns 81\r\n" {
		pass "char function arguments"
	    }
	}
	exec echo 6 > /proc/stap_test_cmd
	expect {
	    -timeout 5
	    "yyy_str Hello-System-Tap\r\nyyy_str returns XYZZY\r\n" {
		pass "string function arguments"
	    }
	}
    }
    eof {}
}
send "\003"
close
wait
