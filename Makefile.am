# Makefile.am --- automake input file for systemtap
## process this file with automake to produce Makefile.in

AM_MAKEFLAGS = 'CXXFLAGS=$(CXXFLAGS)' 'LDFLAGS=$(LDFLAGS)'

bin_PROGRAMS = 
noinst_PROGRAMS = stap
stap_SOURCES = parse.cxx staptree.cxx elaborate.cxx translate.cxx main.cxx
AM_CXXFLAGS = -Wall

# Get extra libs as needed
LDADD =

# Set autoconf Makefile.in variables correctly
datadir = @datadir@
stapdatadir = @datadir@/systemtap

# stuff for compiling gawk/pgawk
DEFPATH="\".$(PATH_SEPARATOR)$(stapdatadir)\""
DEFS= -DDEFPATH=$(DEFPATH) -DHAVE_CONFIG_H

p=$(srcdir)/testsuite/parse
s=$(srcdir)/testsuite/sem
TESTS = $(wildcard $(p)ok/*.stp) $(wildcard $(p)ko/*.stp) \
	$(wildcard $(s)ok/*.stp) $(wildcard $(s)ko/*.stp)
XFAIL_TESTS = $(wildcard $(p)ko/*.stp) $(wildcard $(s)ko/*.stp)

TESTS_ENVIRONMENT = $(srcdir)/runtest.sh

gcov:
	@$(MAKE) clean CXXFLAGS="-g -fprofile-arcs -ftest-coverage" all check
	@gcov *.gcno >/dev/null 2>&1
	@rm -f *.gcno *.gcda
	@rm -f `ls -1 *.gcov | fgrep -v .cxx.gcov`
	ls -l *.cxx.gcov

clean-local:
	rm -f *.gcov *.gcno *.gcda

