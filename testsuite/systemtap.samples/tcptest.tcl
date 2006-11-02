#!/usr/bin/tclsh

set host            localhost 
set port            11900

proc receiver {sock addr port} {
   fconfigure $sock -encoding binary -buffersize 16
   while {[read $sock 16] != {}} { }
   exit
}

proc sender { } {
   set tst_str      "1234567890123456"
   after 2000
   while {[catch {set sock [socket $::host $::port]}]} {}
   fconfigure $sock -encoding binary -buffersize 16
   for {set i 6400} {$i > 0} {incr i -1} {
      puts -nonewline $sock $tst_str;
   }
   return 0 
}

if {[llength $argv] == 0} { 
   socket -server receiver $port
   after 30000 set thirty_secs timeout 
   vwait thirty_secs 
} else {
   exec $argv0 &
   sender 
}
