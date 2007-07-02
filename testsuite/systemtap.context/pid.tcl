spawn stap pid.stp
expect {
    -timeout 240
    "READY" {
	set pid [exec echo 1 > /proc/stap_test_cmd &]
	set uid [exec id -ru] 
	set gid [exec id -rg] 
	set euid [exec id -u] 
	set egid [exec id -g] 
	expect {
            -timeout 5
            "execname: echo\r\n" {
                pass "execname"
		exp_continue
            }
	    "pexecname: expect\r\n" {
                pass "pexecname"
		exp_continue
	    }
	    "pid: $pid\r\n" {
		pass "pid"
		exp_continue
	    }
	    -re {ppid: [^\r\n]+\r\n} {
		pass "ppid"
		exp_continue
	    }
	    "tid: $pid\r\n" {
		pass "tid"
		exp_continue
	    }
	    "uid: $uid\r\n" {
		pass "uid"
		exp_continue
	    }
	    "euid: $euid\r\n" {
		pass "euid"
		exp_continue
	    }
	    "gid: $gid\r\n" {
		pass "gid"
		exp_continue
	    }
	    "egid: $egid\r\n" {
		pass "egid"
		exp_continue
	    }
	    eof {}
        }
    }
    eof {}
}
catch {close}
wait
