set tests [list execname pexecname pid ppid tid uid euid gid egid]
spawn stap $srcdir/$subdir/pid.stp
#exp_internal 1
expect {
    -timeout 60
    "READY" {
	set pid [exec echo 1 > /proc/stap_test_cmd &]
	set ppid {[0-9]*}
	set uid [exec id -ru]
	set gid [exec id -rg]
	set euid [exec id -u]
	set egid [exec id -g]
	set results [list "execname: echo\r\n" "pexecname: expect\r\n" "pid: $pid\r\n" "ppid: $ppid\r\n" "tid: $pid\r\n" "uid: $uid\r\n" "euid: $euid\r\n" "gid: $gid\r\n" "egid: $egid\r\n"]
	
	set i 0
	foreach t $tests {
	    expect {
		-timeout 5
		-re [lindex $results $i] {
		    pass $t
		}
		timeout {fail "$t - timeout"}
		eof {fail "$t - unexpected EOF"}
	    }
	    incr i
	}
    }
    timeout {fail "all pid tests - timeout"}
    eof {fail "all pid tests - unexpected EOF"}
}
catch {close}
wait
