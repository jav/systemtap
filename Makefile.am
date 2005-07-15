# Makefile.am --- automake input file for systemtap
## process this file with automake to produce Makefile.in

AM_MAKEFLAGS = 'CXXFLAGS=$(CXXFLAGS)' 'LDFLAGS=$(LDFLAGS)'

bin_PROGRAMS = stap
stap_SOURCES = main.cxx \
	parse.cxx staptree.cxx elaborate.cxx translate.cxx \
	tapsets.cxx buildrun.cxx

pkglib_PROGRAMS = stpd
stpd_SOURCES = runtime/stpd/stpd.c runtime/stpd/librelay.c
stpd_LDADD = -lpthread

AM_CXXFLAGS = -Wall

# Get extra libs as needed
LDADD =

EXTRA_DIST=testsuite runtime
dist-hook:
	find $(distdir)/testsuite -name CVS -o -name '*~' -o -name '.#*' | xargs rm -rf

install-data-local:
	mkdir -p $(DESTDIR)$(pkgdatadir)
	cp -rp $(srcdir)/runtime $(DESTDIR)$(pkgdatadir)
	find $(DESTDIR)$(pkgdatadir) -name CVS -o -name '*~' -o -name '.#*' | xargs rm -rf

p=$(srcdir)/testsuite/parse
s=$(srcdir)/testsuite/sem
t=$(srcdir)/testsuite/trans
TESTS = $(wildcard $(p)ok/*.stp) $(wildcard $(p)ko/*.stp) \
	$(wildcard $(s)ok/*.stp) $(wildcard $(s)ko/*.stp) \
	$(wildcard $(t)ok/*.stp) $(wildcard $(t)ko/*.stp)
XFAIL_TESTS = $(wildcard $(p)ko/*.stp) \
	$(wildcard $(s)ko/*.stp) \
	$(wildcard $(t)ko/*.stp)

TESTS_ENVIRONMENT = $(srcdir)/runtest.sh

gcov:
	@$(MAKE) clean CXXFLAGS="-g -fprofile-arcs -ftest-coverage" all check
	@gcov *.gcno >/dev/null 2>&1
	@rm -f *.gcno *.gcda
	@rm -f `ls -1 *.gcov | fgrep -v .cxx.gcov`
	ls -l *.cxx.gcov

clean-local:
	rm -f *.gcov *.gcno *.gcda

