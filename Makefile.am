# Makefile.am --- automake input file for systemtap
## process this file with automake to produce Makefile.in

AM_MAKEFLAGS = 'CXXFLAGS=$(CXXFLAGS)' 'LDFLAGS=$(LDFLAGS)'

bin_PROGRAMS = stap
stap_SOURCES = parse.cxx staptree.cxx
AM_CXXFLAGS = -Wall

# Get extra libs as needed
LDADD =

# Set autoconf Makefile.in variables correctly
datadir = @datadir@
stapdatadir = @datadir@/systemtap

# stuff for compiling gawk/pgawk
DEFPATH="\".$(PATH_SEPARATOR)$(stapdatadir)\""
DEFS= -DDEFPATH=$(DEFPATH) -DHAVE_CONFIG_H

pok=$(srcdir)/testsuite/parseok
pko=$(srcdir)/testsuite/parseko
TESTS = $(wildcard $(pok)/*.stp) $(wildcard $(pko)/*.stp)
XFAIL_TESTS = $(wildcard $(pko)/*.stp)
