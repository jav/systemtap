# Makefile.am --- automake input file for systemtap
## process this file with automake to produce Makefile.in

# we don't maintain a ChangeLog, which makes us non-GNU -> foreign
AUTOMAKE_OPTIONS = no-dist foreign

pkglibexecdir = ${libexecdir}/${PACKAGE}
oldincludedir = ${includedir}/sys

AM_CPPFLAGS = -DBINDIR='"$(bindir)"' -DPKGDATADIR='"${pkgdatadir}"' -DPKGLIBDIR='"$(pkglibexecdir)"'

AM_CFLAGS = -D_GNU_SOURCE -fexceptions -Wall -Werror -Wunused -Wformat=2 -W
AM_CXXFLAGS = -Wall -Werror

man_MANS =  stap.1 \
stapprobes.3stap stapfuncs.3stap stapvars.3stap stapex.3stap \
staprun.8 \
man/stapprobes.iosched.3stap man/stapprobes.netdev.3stap \
man/stapprobes.nfs.3stap man/stapprobes.nfsd.3stap \
man/stapprobes.pagefault.3stap man/stapprobes.kprocess.3stap \
man/stapprobes.rpc.3stap man/stapprobes.scsi.3stap \
man/stapprobes.signal.3stap man/stapprobes.socket.3stap \
man/stapprobes.tcp.3stap man/stapprobes.udp.3stap
# see also configure.ac

bin_PROGRAMS = stap staprun
bin_SCRIPTS = stap-report
oldinclude_HEADERS = includes/sys/sdt.h
if BUILD_SERVER
man_MANS += stap-server.8 
bin_PROGRAMS += stap-client-connect stap-server-connect
bin_SCRIPTS += stap-client stap-serverd stap-server stap-find-servers \
	       stap-start-server stap-find-or-start-server stap-stop-server \
	       stap-gen-server-cert stap-add-server-cert
endif
bin_SCRIPTS += dtrace
stap_SOURCES = main.cxx \
	parse.cxx staptree.cxx elaborate.cxx translate.cxx \
	tapsets.cxx buildrun.cxx loc2c.c hash.cxx mdfour.c \
	cache.cxx util.cxx coveragedb.cxx dwarf_wrappers.cxx
stap_LDADD = @stap_LIBS@ @sqlite3_LIBS@

BUILT_SOURCES =
CLEANFILES =
stap_DEPENDENCIES =

# Arrange for git_version.h to be regenerated at every "make".
# Code fragment is based upon RadeonHD.am.

# The stamp file which is never created ensures that git_version.h is updated
# before every build. Having git_version.h in foo_SOURCES ensures a recompile
# of foo-bar.c if it is newer than the foo-bar.o file. Using noinst_foo_SOURCES
# instead of foo_SOURCES prevents shipping git_version.h in dist tarballs,
# which may cause false GIT_FOO readings.
BUILT_SOURCES += git_version.stamp
CLEANFILES += git_version.h
GIT_VERSION_CMD = $(SHELL) $(top_srcdir)/git_version.sh
git_version.stamp:
	@if test -f "$(srcdir)/git_version.h"; then \
		if test -f "git_version.h"; then :; \
		else \
			cp "$(srcdir)/git_version.h" "git_version.h"; \
		fi; \
	fi
	$(GIT_VERSION_CMD) -k -s $(top_srcdir) -o git_version.h
	@if test -s "$(srcdir)/git_version.h"; then \
		if cmp "$(srcdir)/git_version.h" "git_version.h"; then :; \
		else \
			echo "Error: $(srcdir)/git_version.h and git_version.h differ."; \
			echo "       You probably want to remove the former."; \
			exit 1; \
		fi; \
	fi

git_version.h:
	$(srcdir)/git_version.sh -k --srcdir $(srcdir) -o git_version.h

cscope:
	cd $(srcdir) && \
	(echo -q ; git ls-files '*.cxx' '*.c' '*.h' | grep -v '^testsuite' ) > cscope.files && \
	cscope -b -q


stap_CFLAGS = $(AM_CFLAGS) @PIECFLAGS@
stap_CXXFLAGS = $(AM_CXXFLAGS) @PIECXXFLAGS@
stap_CPPFLAGS = $(AM_CPPFLAGS)
stap_LDFLAGS = $(AM_LDFLAGS) @PIELDFLAGS@

if BUILD_SERVER
stap_client_connect_LDFLAGS = $(AM_LDFLAGS)
stap_server_connect_LDFLAGS = $(AM_LDFLAGS)
endif

PHONIES =
if BUILD_ELFUTILS
stap_CPPFLAGS += -Iinclude-elfutils
stap_LDFLAGS += -Llib-elfutils -Wl,-rpath-link,lib-elfutils \
		-Wl,--enable-new-dtags,-rpath,$(pkglibdir)


BUILT_SOURCES += stamp-elfutils
CLEANFILES += stamp-elfutils
stamp-elfutils: config.status
	$(MAKE) $(AM_MAKEFLAGS) -C build-elfutils all bin_PROGRAMS=
	for dir in libelf libebl libdw libdwfl backends; do \
	  $(MAKE) $(AM_MAKEFLAGS) -C build-elfutils/$$dir bin_PROGRAMS= install; \
	done
	touch $@
stap_DEPENDENCIES += lib-elfutils/libdw.so
lib-elfutils/libdw.so: stamp-elfutils ;

PHONIES += install-elfutils
install-elfutils:
	mkdir -p $(DESTDIR)$(pkglibdir)
	for file in lib-elfutils/*.so* lib-elfutils/${PACKAGE_NAME}/*.so*; do \
	   $(INSTALL_PROGRAM) $$file $(DESTDIR)$(pkglibdir); \
	done
install-exec-local: install-elfutils
endif

if BUILD_SERVER
install-exec-local: install-scripts

PHONIES += install-scripts
# scripts should be installed before this rule is run
install-scripts: install-binSCRIPTS
	for f in $(bin_SCRIPTS); do \
	   sed -i -e "/INSTALL-HOOK/d;s,exec_prefix=,exec_prefix=$(exec_prefix)/bin/,;s,sysconfdir=.*,sysconfdir=$(sysconfdir)," $(DESTDIR)$(bindir)/$$f; \
	done
endif

staprun_SOURCES = runtime/staprun/staprun.c runtime/staprun/staprun_funcs.c\
	runtime/staprun/ctl.c runtime/staprun/common.c

staprun_CPPFLAGS = $(AM_CPPFLAGS)
staprun_CFLAGS = @PROCFLAGS@ $(AM_CFLAGS) @PIECFLAGS@ -DSINGLE_THREADED -fno-strict-aliasing
staprun_LDFLAGS = $(AM_LDFLAGS) @PIELDFLAGS@
staprun_LDADD = @PROCFLAGS@

stapio_SOURCES = runtime/staprun/stapio.c \
	runtime/staprun/mainloop.c runtime/staprun/common.c \
	runtime/staprun/ctl.c \
	runtime/staprun/relay.c runtime/staprun/relay_old.c
stapio_CFLAGS = @PROCFLAGS@ $(AM_CFLAGS) @PIECFLAGS@ -fno-strict-aliasing
stapio_LDFLAGS = $(AM_LDFLAGS) @PIELDFLAGS@
stapio_LDADD = @PROCFLAGS@ -lpthread

if BUILD_SERVER
stap_client_connect_SOURCES = stap-client-connect.c
stap_client_connect_CFLAGS = -Wall -Werror $(nss_CFLAGS) $(nspr_CFLAGS)
stap_client_connect_LDADD = -lssl3

stap_server_connect_SOURCES = stap-server-connect.c
stap_server_connect_CFLAGS = -Wall -Werror $(nss_CFLAGS) $(nspr_CFLAGS)
stap_server_connect_LDADD = -lssl3
endif

install-exec-hook:
	if [ `id -u` -eq 0 ]; then chmod 04111 "$(DESTDIR)$(bindir)/staprun"; fi

# Why the "id -u" condition?  This way, an unprivileged user can run
# make install, and have "sudo stap ...." or "sudo staprun ...." work later.


pkglibexec_PROGRAMS = stapio
CLEANFILES +=  $(pkglibexec_PROGRAMS)

noinst_PROGRAMS = loc2c-test
loc2c_test_SOURCES = loc2c-test.c loc2c.c
loc2c_test_CFLAGS = $(stap_CFLAGS)
loc2c_test_CPPFLAGS = $(stap_CPPFLAGS)
loc2c_test_LDFLAGS = $(stap_LDFLAGS)
loc2c_test_LDADD = $(stap_LDADD)

# crash(8) extension
if BUILD_CRASHMOD
STAPLOG=staplog.so

$(STAPLOG): staplog.c
	$(CC) $(staplog_CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) -shared -rdynamic \
		$(AM_LDFLAGS) $(LDFLAGS) -fPIC -o $@ $<
all-local: $(STAPLOG) example_index
install-exec-local: $(STAPLOG)
	$(MKDIR_P) $(DESTDIR)$(pkglibdir)
	$(INSTALL) $(STAPLOG) $(DESTDIR)$(pkglibdir)
else
all-local: example_index
endif

# Get extra libs as needed
LDADD =

EXAMPLE_SOURCE_DIR = $(srcdir)/testsuite/systemtap.examples
EXAMPLE_META_FILES = $(EXAMPLE_SOURCE_DIR)/*/*.meta

example_index: $(EXAMPLE_SOURCE_DIR)/index.html

$(EXAMPLE_SOURCE_DIR)/index.html: $(EXAMPLE_META_FILES) $(EXAMPLE_INDEX_GEN)
	@echo "The script example index is outdated, run"
	@echo "    cd $(EXAMPLE_SOURCE_DIR) && \\ "
	@echo "    perl examples-index-gen.pl"
	@echo "to regenerate it."

install-data-local:
	(cd $(srcdir)/runtime; for f in *.[ch]; do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/runtime/$$f; done)
	(cd $(srcdir)/runtime/unwind; find . \( -name '*.[ch]' \) -print \
		| while read f; do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/runtime/unwind/$$f; done)
	(cd $(srcdir)/runtime/transport; for f in *.[ch]; \
		do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/runtime/transport/$$f; done)
	(cd $(srcdir)/runtime/uprobes; for f in Makefile *.[ch]; \
		do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/runtime/uprobes/$$f; done)
	(cd $(srcdir)/runtime/uprobes2; for f in *.[ch]; \
		do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/runtime/uprobes2/$$f; done)
	(cd $(srcdir)/tapset; find . \( -name '*.stp' -o -name README \) -print \
		| while read f; do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/tapset/$$f; done)
	(cd $(srcdir)/testsuite/systemtap.examples; find . -type f -print \
		| egrep -v 'check.exp|ChangeLog|examples-index-gen.pl' \
		| while read f; do if test -x $$f; then \
			i_cmd="$(INSTALL_PROGRAM)"; else \
			i_cmd="$(INSTALL_DATA)"; fi; \
			$$i_cmd -D $$f $(DESTDIR)$(docdir)/examples/$$f; done)
	test -e $(DESTDIR)$(sysconfdir)/systemtap || mkdir -p $(DESTDIR)$(sysconfdir)/systemtap

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
	rm -rf ${PACKAGE_TARNAME}-${PACKAGE_VERSION}
	rm -rf $(TEST_COV_DIR)
	rm -rf stap.info
	rm -rf staplog.so

distclean-local:
	rm -rf build-elfutils include-elfutils lib-elfutils

uninstall-local:
	rm -rf $(DESTDIR)$(pkgdatadir)
	rm -rf $(DESTDIR)$(libexecdir)/$(PACKAGE)
	rm -rf $(DESTDIR)$(libdir)/$(PACKAGE)
	-rm -rf $(DESTDIR)$(localstatedir)/cache/$(PACKAGE)
	@list='$(EXAMPLE_SRC)'; for f in $$list; do p=`basename $$f`; \
	  echo " rm -f '$(EXAMPLE_INSTALL_DIR)/$$p'"; \
	  rm -f "$(EXAMPLE_INSTALL_DIR)/$$p"; \
	done
	@list='$(DEMO_SRC)'; for f in $$list; do p=`basename $$f`; \
	  echo " rm -f '$(DEMO_INSTALL_DIR)/$$p'"; \
	  rm -f "$(DEMO_INSTALL_DIR)/$$p"; \
	done
	@list='$(SAMPLE_SRC)'; for f in $$list; do p=`basename $$f`; \
	  echo " rm -f '$(SAMPLE_INSTALL_DIR)/$$p'"; \
	  rm -f "$(SAMPLE_INSTALL_DIR)/$$p"; \
	done
	rm -rf $(DESTDIR)$(sysconfdir)/systemtap
# XXX: leaves behind man pages

SUBDIRS = doc
DIST_SUBDIRS = testsuite grapher $(SUBDIRS)

check-local:
	SRCDIR=`cd $(srcdir); pwd`; \
        PWD=`pwd`; \
	$(MAKE) -C testsuite check SYSTEMTAP_RUNTIME=$$SRCDIR/runtime SYSTEMTAP_TAPSET=$$SRCDIR/tapset LD_LIBRARY_PATH=$$PWD/lib-elfutils:$$PWD/lib-elfutils/systemtap SYSTEMTAP_PATH=$$PWD SYSTEMTAP_INCLUDES=$$PWD/includes RUNTESTFLAGS="$(RUNTESTFLAGS)";

installcheck:
	if test \! -e $(DESTDIR)$(bindir)/stap; then \
	  echo $(DESTDIR)$(bindir)/stap doesn\'t exist, run make install; \
	  exit -1; \
	fi; \
	if test $(builddir)/stap -nt $(DESTDIR)$(bindir)/stap; then \
	  echo "$(DESTDIR)$(bindir)/stap is not recent, run make install"; \
	  exit -1; \
	fi;
	$(MAKE) -C testsuite installcheck RUNTESTFLAGS="$(RUNTESTFLAGS)"


# Any extra flags, such as:
#     --define "with_docs 0"
#     --define "with_bundled_elfutils 1" --define "elfutils_version 0.135"
RPMBUILDFLAGS=

PHONIES += dist-gzip

.PHONY: $(PHONIES)

dist-gzip:
	cd $(srcdir); git status | grep working.directory.clean || (echo "You should commit your changes before 'make rpm'.")
	(cd $(srcdir); git archive --prefix=systemtap-$(VERSION)/ --format=tar HEAD) | gzip > systemtap-$(VERSION).tar.gz

rpm: dist-gzip
	mkdir -p `rpm --eval %_specdir`
	mkdir -p `rpm --eval %_srcrpmdir`
	mkdir -p `rpm --eval %_rpmdir`
	mkdir -p `rpm --eval %_builddir`
	mkdir -p `rpm --eval %_buildroot`
	rpmbuild --define "_sourcedir $(PWD)/" -ta systemtap-$(VERSION).tar.gz $(RPMBUILDFLAGS)
