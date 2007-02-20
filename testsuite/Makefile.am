# Makefile.am --- automake input file for systemtap
## process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = dejagnu

STAP_TESTDIR = ..

# The stap symlink is to enable "#! stap" test scripts.
all-local:
	@ln -sf $(STAP_TESTDIR)/stap .

clean-local:
	@rm -f ./stap site.exp systemtap.sum systemtap.log
	@rm -rf .systemtap .cache_test

check-local:

installcheck-local: clean site.exp
	echo "set prefix $(DESTDIR)$(prefix)" >> site.exp
	$(MAKE) AM_RUNTESTFLAGS="--tool_opts install" STAP_TESTDIR=$(DESTDIR)$(bindir) check
