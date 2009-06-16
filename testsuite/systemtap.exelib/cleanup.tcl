# Remove exes, libs and (possible) separate .debug files
catch {exec rm -f $testexe ${testexe}.debug}
catch {exec rm -f $testlib ${testlib}.debug}
