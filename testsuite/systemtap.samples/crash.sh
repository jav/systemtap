#! /bin/sh

crash --readnow << END
mod -s testlog testlog.ko
extend $1/staplog.so
staplog testlog
exit
END
