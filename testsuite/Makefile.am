# Makefile.am --- automake input file for systemtap testsuite
## process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = dejagnu

# The stap symlink is to enable "#! stap" test scripts.
all-local:
	@echo Run \"make check\" or \"make installcheck\".

clean-local:
	@rm -f ./stap site.exp systemtap.sum systemtap.log
	@rm -rf .systemtap .cache_test

installcheck-local: clean site.exp
	$(MAKE) AM_RUNTESTFLAGS="--tool_opts install" check-DEJAGNU

SRCDIR = $(shell cd $(srcdir); pwd)

# These values point the test suite to the install tree, and 
# are overridden by "make check" from full source/build tree
SYSTEMTAP_RUNTIME=$(DESTDIR)$(pkgdatadir)/runtime
SYSTEMTAP_TAPSET=$(DESTDIR)$(pkgdatadir)/tapset
LD_LIBRARY_PATH=$(DESTDIR)$(libdir)/systemtap
SYSTEMTAP_PATH=$(DESTDIR)$(bindir)

RUNTEST="env SYSTEMTAP_RUNTIME=$(SYSTEMTAP_RUNTIME) SYSTEMTAP_TAPSET=$(SYSTEMTAP_TAPSET) LD_LIBRARY_PATH=$(LD_LIBRARY_PATH) PATH=$(SYSTEMTAP_PATH):$$PATH runtest"
