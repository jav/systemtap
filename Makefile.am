# Makefile.am --- automake input file for systemtap
## process this file with automake to produce Makefile.in

AM_CPPFLAGS = -DPKGLIBDIR='"${libexecdir}/${PACKAGE}"' \
	      -DPKGDATADIR='"${pkgdatadir}"'

AM_CFLAGS = -std=gnu99 -D_GNU_SOURCE \
	    -Wall -Werror -Wshadow -Wunused -Wformat=2 -W
AM_CXXFLAGS = -Wall

dist_man_MANS = stap.1
bin_PROGRAMS = stap
stap_SOURCES = main.cxx \
	parse.cxx staptree.cxx elaborate.cxx translate.cxx \
	tapsets.cxx buildrun.cxx loc2c.c
stap_LDADD = @stap_LIBS@

stap_CXXFLAGS = -Werror $(AM_CXXFLAGS)

# stpd goes into $libexec/$package, so libexec_PROGRAMS would
# put it into the wrong location
noinst_PROGRAMS = stpd
stpd_SOURCES = runtime/stpd/stpd.c runtime/stpd/librelay.c
stpd_LDADD = -lpthread

# Get extra libs as needed
LDADD =

EXTRA_DIST = testsuite runtest.sh $(wildcard $(srcdir)/*.h) systemtap.spec \
	     runtime tapset
dist-hook:
	find $(distdir) -name CVS -o -name '*~' -o -name '.#*' | xargs rm -rf
	find $(distdir) -name '*.o' -o -name '*.ko' -o -name '*.cmd' -o -name '*.mod.c' -o -name '.??*' | xargs rm -rf

install-exec-local:
	mkdir -p $(DESTDIR)$(libexecdir)/systemtap
	$(INSTALL) stpd $(DESTDIR)$(libexecdir)/systemtap

install-data-local:
	mkdir -p $(DESTDIR)$(pkgdatadir)/runtime/transport
	cp -rp $(srcdir)/runtime/*.[ch] $(srcdir)/runtime/Doxyfile \
	       $(DESTDIR)$(pkgdatadir)/runtime
	tar -f - -c --exclude=CVS --exclude='*~' --exclude='.#*' \
	    -C $(srcdir)/runtime probes \
	| (cd $(DESTDIR)$(pkgdatadir)/runtime && tar xpf -)
	tar -f - -c --exclude=CVS --exclude='*~' --exclude='.#*' \
	    -C $(srcdir)/runtime transport \
	| (cd $(DESTDIR)$(pkgdatadir)/runtime && tar xpf -)
	tar -f - -c --exclude=CVS --exclude='*~' --exclude='.#*' \
	    -C $(srcdir)/runtime relayfs \
	| (cd $(DESTDIR)$(pkgdatadir)/runtime && tar xpf -)
	-mkdir $(DESTDIR)$(pkgdatadir)/tapset
	(cd $(srcdir)/tapset ; find . -name '*.stp' | cpio -pdmv $(DESTDIR)$(pkgdatadir)/tapset)

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

gcov:
	@$(MAKE) clean CXXFLAGS="-g -fprofile-arcs -ftest-coverage" all check
	@gcov *.gcno >/dev/null 2>&1
	@rm -f *.gcno *.gcda
	@rm -f `ls -1 *.gcov | fgrep -v .cxx.gcov`
	ls -l *.cxx.gcov

docs:	runtime/Doxyfile
	cd runtime && doxygen Doxyfile

rpm:	dist docs
	rpmbuild -ta ${PACKAGE_TARNAME}-${PACKAGE_VERSION}.tar.gz

clean-local:
	rm -f *.gcov *.gcno *.gcda ${PACKAGE_TARNAME}-*.tar.gz
	rm -rf testsuite
	rm -rf ${PACKAGE_TARNAME}-${PACKAGE_VERSION}
