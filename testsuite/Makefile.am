# Makefile.am --- automake input file for systemtap testsuite
## process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = dejagnu no-dist

all-local:
	@echo Run \"make check\" or \"make installcheck\".
	@if test -n "$(DEJAZILLA)"; then echo Test results will be emailed to $(DEJAZILLA); fi

clean-local:
	-rm -f ./stap site.exp systemtap.sum systemtap.log
	-rm -rf .systemtap* .cache_test* 2>/dev/null

DEJAZILLA=@dejazilla@

TOOL_OPTS=

# automake's dejagnu library already runs check-DEJAGNU before check-local
# That's why we need to add "execrc" to $(RUNTEST) - to ensure that this
# subtarget gets run even if runtest per se exits with a failure.
check-local:
	if test -n "$(DEJAZILLA)"; then mail $(DEJAZILLA) < systemtap.sum; fi

# but installcheck does not follow an implicit check-DEJAGNU, go figure
installcheck: site.exp
	-$(MAKE) $(AM_MAKEFLAGS) check-DEJAGNU RUNTESTFLAGS="$(RUNTESTFLAGS) --tool_opts \'install $(TOOL_OPTS)\'"
	if test -n "$(DEJAZILLA)"; then mail $(DEJAZILLA) < systemtap.sum; fi

# $(srcdir)/These values point the test suite to the install tree, and
# are overridden by "make check" from full source/build tree
SYSTEMTAP_RUNTIME=$(DESTDIR)$(pkgdatadir)/runtime
SYSTEMTAP_TAPSET=$(DESTDIR)$(pkgdatadir)/tapset
LD_LIBRARY_PATH=$(DESTDIR)$(libdir)/systemtap
CRASH_LIBDIR=$(DESTDIR)$(libdir)/systemtap
SYSTEMTAP_PATH=$(DESTDIR)$(bindir)

RUNTESTDEFAULTFLAGS = --tool $$tool --tool_opts \'$(TOOL_OPTS)\' --srcdir $$srcdir
EXPECT = expect
RUNTEST="env SYSTEMTAP_RUNTIME=$(SYSTEMTAP_RUNTIME) SYSTEMTAP_TAPSET=$(SYSTEMTAP_TAPSET) LD_LIBRARY_PATH=$(LD_LIBRARY_PATH) CRASH_LIBDIR=$(CRASH_LIBDIR) PATH=$(SYSTEMTAP_PATH):$$PATH $(srcdir)/execrc runtest"
