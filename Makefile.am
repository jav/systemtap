# Makefile.am --- automake input file for systemtap
## process this file with automake to produce Makefile.in

AM_MAKEFLAGS = 'CXXFLAGS=$(CXXFLAGS)' 'LDFLAGS=$(LDFLAGS)'

bin_PROGRAMS = 
noinst_PROGRAMS = parsetest semtest
parsetest_SOURCES = parse.cxx staptree.cxx parsetest.cxx
semtest_SOURCES = parse.cxx staptree.cxx semtest.cxx
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
