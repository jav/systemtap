package require tcltest
namespace import -force tcltest::*

puts "Running all SystemTap tests"

#puts [tcltest::configure]
#puts [tcltest::configure -file]

tcltest::testsDirectory [file dir [info script]]
tcltest::runAllTests

puts "All tests completed"
