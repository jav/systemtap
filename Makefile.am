# Makefile.am --- automake input file for systemtap
## process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = dist-bzip2

pkglibexecdir = ${libexecdir}/${PACKAGE}

AM_CPPFLAGS = -DBINDIR='"$(bindir)"' -DPKGDATADIR='"${pkgdatadir}"' -DPKGLIBDIR='"$(pkglibexecdir)"'

AM_CFLAGS = -D_GNU_SOURCE -fexceptions -Wall -Werror -Wunused -Wformat=2 -W
AM_CXXFLAGS = -Wall -Werror

dist_man_MANS = stap.1 stapprobes.5 stapfuncs.5 stapex.5 staprun.8 man/stapprobes.iosched.5 man/stapprobes.netdev.5 man/stapprobes.nfs.5 man/stapprobes.nfsd.5 man/stapprobes.pagefault.5 man/stapprobes.process.5 man/stapprobes.rpc.5 man/stapprobes.scsi.5 man/stapprobes.signal.5 man/stapprobes.socket.5 man/stapprobes.tcp.5 man/stapprobes.udp.5
bin_PROGRAMS = stap staprun
stap_SOURCES = main.cxx \
	parse.cxx staptree.cxx elaborate.cxx translate.cxx \
	tapsets.cxx buildrun.cxx loc2c.c hash.cxx mdfour.c \
	cache.cxx util.cxx coveragedb.cxx
stap_LDADD = @stap_LIBS@ @sqlite3_LIBS@

BUILT_SOURCES =
CLEANFILES =

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

dist-gitversion: git_version.stamp
	if test -f "git_version.h"; then \
		sed -e 's|^#undef GIT_IS_DIST.*|#define GIT_IS_DIST 1|' \
			"git_version.h" > "$(distdir)/git_version.h"; \
	fi


git_version.h:
	$(srcdir)/git_version.sh -k --srcdir $(srcdir) -o git_version.h

cscope:
	cd $(srcdir) && \
	(echo -q ; git ls-files '*.cxx' '*.c' '*.h' | grep -v '^testsuite' ) > cscope.files && \
	cscope -b -q


stap_CXXFLAGS = $(AM_CXXFLAGS)
stap_CPPFLAGS = $(AM_CPPFLAGS)
stap_LDFLAGS = $(AM_LDFLAGS) @PIELDFLAGS@
staprun_LDFLAGS = $(AM_LDFLAGS) @PIELDFLAGS@
stapio_CPPFLAGS = $(AM_CPPFLAGS)
stapio_LDFLAGS = $(AM_LDFLAGS) @PIELDFLAGS@

if BUILD_ELFUTILS
# This tells automake's "make distcheck" what we need to compile.
DISTCHECK_CONFIGURE_FLAGS = --with-elfutils=$(elfutils_abs_srcdir)

stap_CPPFLAGS += -Iinclude-elfutils
stap_LDFLAGS += -Llib-elfutils -Wl,-rpath-link,lib-elfutils \
		-Wl,--enable-new-dtags,-rpath,$(pkglibdir)


BUILT_SOURCES += stamp-elfutils
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
	mkdir -p $(DESTDIR)$(pkglibdir)
	for file in lib-elfutils/*.so* lib-elfutils/${PACKAGE_NAME}/*.so*; do \
	   $(INSTALL_PROGRAM) $$file $(DESTDIR)$(pkglibdir); \
	done
install-exec-local: install-elfutils
endif

staprun_SOURCES = runtime/staprun/staprun.c runtime/staprun/staprun_funcs.c\
	runtime/staprun/ctl.c runtime/staprun/common.c \
	runtime/staprun/cap.c

staprun_CPPFLAGS = $(AM_CPPFLAGS)
staprun_CFLAGS = @PROCFLAGS@ $(AM_CFLAGS) -DSINGLE_THREADED
staprun_LDADD = @PROCFLAGS@ @cap_LIBS@

stapio_SOURCES = runtime/staprun/stapio.c \
	runtime/staprun/mainloop.c runtime/staprun/common.c \
	runtime/staprun/ctl.c \
	runtime/staprun/relay.c runtime/staprun/relay_old.c 
stapio_CFLAGS = @PROCFLAGS@ $(AM_CFLAGS)
stapio_LDADD = @PROCFLAGS@ -lpthread

install-exec-hook:
	if [ `id -u` -eq 0 ]; then chmod 04111 "$(DESTDIR)$(bindir)/staprun"; fi

# Why the "id -u" condition?  This way, an unprivileged user can run
# make install, and have "sudo stap ...." or "sudo staprun ...." work later.


pkglibexec_PROGRAMS = stapio
CLEANFILES +=  $(pkglibexec_PROGRAMS)

noinst_PROGRAMS = loc2c-test
loc2c_test_SOURCES = loc2c-test.c loc2c.c
loc2c_test_CPPFLAGS = $(stap_CPPFLAGS)
loc2c_test_LDFLAGS = $(stap_LDFLAGS)
loc2c_test_LDADD = $(stap_LDADD)

# crash(8) extension
if BUILD_CRASHMOD
STAPLOG=staplog.so

$(STAPLOG): staplog.c
	$(CC) $(staplog_CPPFLAGS) $(AM_CFLAGS) $(CFLAGS) -shared -rdynamic \
		$(AM_LDFLAGS) $(LDFLAGS) -fPIC -o $@ $<
all-local: $(STAPLOG)
install-exec-local: $(STAPLOG)
	$(MKDIR_P) $(DESTDIR)$(pkglibdir)
	$(INSTALL) $(STAPLOG) $(DESTDIR)$(pkglibdir)
endif

# Get extra libs as needed
LDADD =

EXTRA_DIST = buildrun.h elaborate.h loc2c.h session.h \
	     parse.h staptree.h tapsets.h translate.h \
	     cache.h hash.h mdfour.h util.h staplog.c coveragedb.h \
	     testsuite systemtap.spec runtime tapset \
	     git_version.h git_version.sh

EXAMPLE_DEST_DIR = $(distdir)/examples
SAMPLE_DEST_DIR = $(EXAMPLE_DEST_DIR)/samples

EXAMPLE_SRC = $(srcdir)/testsuite/systemtap.examples

SAMPLE_SRC = $(srcdir)/testsuite/systemtap.samples/iotask.stp \
	     $(srcdir)/testsuite/systemtap.samples/kmalloc-stacks.stp \
	     $(srcdir)/testsuite/systemtap.samples/kmalloc-top \
	     $(srcdir)/testsuite/systemtap.samples/pfaults.stp \
	     $(srcdir)/testsuite/systemtap.samples/profile.stp \
	     $(srcdir)/testsuite/systemtap.samples/tcp_connections.stp \
	     $(srcdir)/testsuite/systemtap.samples/topsys.stp

dist-add-examples: $(EXAMPLE_SRC)
	rm -rf $(EXAMPLE_DEST_DIR)
	mkdir -p $(EXAMPLE_DEST_DIR)
	cp -a $(EXAMPLE_SRC)/* $(EXAMPLE_DEST_DIR)/.

# Copy some of the testsuite sample scripts to the distdir
# 'examples/samples' directory.
dist-add-samples: $(SAMPLE_SRC) dist-add-examples
	rm -rf $(SAMPLE_DEST_DIR)
	mkdir -p $(SAMPLE_DEST_DIR)
	cp $(SAMPLE_SRC) $(SAMPLE_DEST_DIR)

dist-hook: dist-add-samples dist-gitversion
	find $(distdir) -name CVS -o -name '*~' -o -name '.#*' | xargs rm -rf
	find $(distdir) -name '*.o' -o -name '*.ko' -o -name '*.cmd' -o -name '*.mod.c' -o -name '.??*' | xargs rm -rf
	find $(distdir) -name 'stap' -o -name '*.log' -o -name '*.sum' -o -name 'site.exp' | xargs rm -rf

install-data-local:
	(cd $(srcdir)/runtime; for f in *.[ch]; do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/runtime/$$f; done)
	(cd $(srcdir)/runtime/unwind; find . \( -name '*.[ch]' \) -print \
		| while read f; do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/runtime/unwind/$$f; done)
	(cd $(srcdir)/runtime/transport; for f in *.[ch]; \
		do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/runtime/transport/$$f; done)
	(cd $(srcdir)/runtime/uprobes; for f in Makefile *.[ch]; \
		do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/runtime/uprobes/$$f; done)
	(cd $(srcdir)/tapset; find . \( -name '*.stp' -o -name README \) -print \
		| while read f; do $(INSTALL_DATA) -D $$f $(DESTDIR)$(pkgdatadir)/tapset/$$f; done)

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

uninstall-local:
	rm -rf $(DESTDIR)$(pkgdatadir)
	rm -rf $(DESTDIR)$(libexecdir)/$(PACKAGE)
	rm -rf $(DESTDIR)$(libdir)/$(PACKAGE)
	-rm -rf $(DESTDIR)$(localstatedir)/cache/$(PACKAGE)
# XXX: leaves behind man pages

SUBDIRS = testsuite doc

check:
	SRCDIR=`cd $(srcdir); pwd`; \
	$(MAKE) -C testsuite check SYSTEMTAP_RUNTIME=$$SRCDIR/runtime SYSTEMTAP_TAPSET=$$SRCDIR/tapset LD_LIBRARY_PATH=$(PWD)/lib-elfutils:$(PWD)/lib-elfutils/systemtap SYSTEMTAP_PATH=$(PWD) RUNTESTFLAGS="$(RUNTESTFLAGS)"

installcheck:
	$(MAKE) -C testsuite installcheck RUNTESTFLAGS="$(RUNTESTFLAGS)"

rpm:	systemtap.spec dist
	rpmbuild --define "_sourcedir $(PWD)/" -ba systemtap.spec
