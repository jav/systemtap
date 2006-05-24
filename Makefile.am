# Makefile.am --- automake input file for systemtap
## process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = dist-bzip2

pkglibexecdir = ${libexecdir}/${PACKAGE}

AM_CPPFLAGS = -DPKGLIBDIR='"$(pkglibexecdir)"' -DPKGDATADIR='"${pkgdatadir}"'

AM_CFLAGS = -std=gnu99 -D_GNU_SOURCE -fexceptions \
	    -Wall -Werror -Wshadow -Wunused -Wformat=2 -W
AM_CXXFLAGS = -Wall

dist_man_MANS = stap.1 stapprobes.5 stapfuncs.5 stapex.5 lket.5
bin_PROGRAMS = stap
stap_SOURCES = main.cxx \
	parse.cxx staptree.cxx elaborate.cxx translate.cxx \
	tapsets.cxx buildrun.cxx loc2c.c
stap_LDADD = @stap_LIBS@

stap_CXXFLAGS = -Werror $(AM_CXXFLAGS)
stap_CPPFLAGS = $(AM_CPPFLAGS)
stap_LDFLAGS = $(AM_LDFLAGS)

CLEANFILES =

if BUILD_ELFUTILS
stap_CPPFLAGS += -Iinclude-elfutils
stap_LDFLAGS += -Llib-elfutils -Wl,-rpath-link,lib-elfutils \
		-Wl,--enable-new-dtags,-rpath,$(pkglibdir)
BUILT_SOURCES = stamp-elfutils
CLEANFILES += stamp-elfutils
stamp-elfutils: config.status
	$(MAKE) $(AM_MAKEFLAGS) -C build-elfutils all
	for dir in libelf libebl libdw libdwfl backends; do \
	  $(MAKE) $(AM_MAKEFLAGS) -C build-elfutils/$$dir install; \
	done
	touch $@
stap_DEPENDENCIES = lib-elfutils/libdw.so
lib-elfutils/libdw.so: stamp-elfutils ;

.PHONY: install-elfutils
install-elfutils:
	mkdir -p $(pkglibdir)
	cp -df lib-elfutils/*.so* lib-elfutils/${PACKAGE_NAME}/*.so* \
	   $(pkglibdir)
install-exec-local: install-elfutils
endif

# stpd goes into $libexec/$package, so libexec_PROGRAMS would
# put it into the wrong location
pkglibexec_PROGRAMS = stpd
stpd_SOURCES = runtime/stpd/stpd.c runtime/stpd/librelay.c
stpd_LDADD = -lpthread

pkglibexec_SCRIPTS = stp_check
CLEANFILES += $(pkglibexec_SCRIPTS)

noinst_PROGRAMS = loc2c-test
loc2c_test_SOURCES = loc2c-test.c loc2c.c
loc2c_test_CPPFLAGS = $(stap_CPPFLAGS)
loc2c_test_LDFLAGS = $(stap_LDFLAGS)
loc2c_test_LDADD = $(stap_LDADD)

# Get extra libs as needed
LDADD =

EXTRA_DIST = buildrun.h elaborate.h loc2c.h session.h \
	     parse.h staptree.h tapsets.h translate.h \
	     testsuite runtest.sh systemtap.spec runtime tapset
dist-hook:
	find $(distdir) -name CVS -o -name '*~' -o -name '.#*' | xargs rm -rf
	find $(distdir) -name '*.o' -o -name '*.ko' -o -name '*.cmd' -o -name '*.mod.c' -o -name '.??*' | xargs rm -rf

install-data-local:
#	mkdir -p $(DESTDIR)$(pkgdatadir)/runtime/transport $(DESTDIR)$(pkgdatadir)/runtime/relayfs $(DESTDIR)$(pkgdatadir)/tapset
	(cd $(srcdir)/runtime; for f in *.[ch]; do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/runtime/$$f; done)
	(cd $(srcdir)/runtime/transport; for f in *.[ch]; \
		do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/runtime/transport/$$f; done)
	(cd $(srcdir)/runtime/relayfs; find . -name '*.[ch]' -print \
		| while read f; do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/runtime/relayfs/$$f; done)
	(cd $(srcdir)/tapset; find . -name '*.stp' -print \
		| while read f; do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/tapset/$$f; done)

p=$(srcdir)/testsuite/parse
s=$(srcdir)/testsuite/sem
t=$(srcdir)/testsuite/trans
b=$(srcdir)/testsuite/build
TESTS = $(wildcard $(p)ok/*.stp) $(wildcard $(p)ko/*.stp) \
	$(wildcard $(s)ok/*.stp) $(wildcard $(s)ko/*.stp) \
	$(wildcard $(t)ok/*.stp) $(wildcard $(t)ko/*.stp) \
	$(wildcard $(b)ok/*.stp) $(wildcard $(b)ko/*.stp)
XFAIL_TESTS = $(wildcard $(p)ko/*.stp) \
	$(wildcard $(s)ko/*.stp) \
	$(wildcard $(t)ko/*.stp) \
	$(wildcard $(b)ko/*.stp)

TESTS_ENVIRONMENT = $(srcdir)/runtest.sh

TEST_COV_DIR = coverage

gcov:
	@-$(MAKE) clean CXXFLAGS="-g -fprofile-arcs -ftest-coverage" all check
	@gcov *.gcno >/dev/null 2>&1
	@rm -f `ls -1 *.gcov | fgrep -v .cxx.gcov`
	ls -l *.cxx.gcov

lcov:
	@lcov --directory . -z
	@-$(MAKE) clean CXXFLAGS="-g -fprofile-arcs -ftest-coverage" all check
	@lcov --directory . -c -o stap.info
	@genhtml -o ./$(TEST_COV_DIR) stap.info

clean-local:
	rm -f *.gcov *.gcno *.gcda ${PACKAGE_TARNAME}-*.tar.gz
	rm -rf testresults
	rm -rf ${PACKAGE_TARNAME}-${PACKAGE_VERSION}
	rm -rf $(TEST_COV_DIR)
	rm -rf stap.info

uninstall-local:
	rm -rf $(DESTDIR)$(pkgdatadir)
	rm -rf $(DESTDIR)$(libexecdir)/$(PACKAGE)
	-rm -rf $(DESTDIR)$(localstatedir)/cache/$(PACKAGE)

#SUBDIRS = runtime/lket/b2a
