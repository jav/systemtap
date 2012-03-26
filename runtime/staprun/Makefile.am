# Makefile.am --- automake input file for systemtap runtime tools

AM_CFLAGS = -Wall -Werror -Wunused -W -Wformat=2 -Wno-format-nonliteral
AM_CXXFLAGS = -Wall -Werror -Wunused -W -Wformat=2 -Wno-format-nonliteral
AM_CPPFLAGS = -D_GNU_SOURCE
AM_CPPFLAGS += -I$(srcdir)/../../includes
AM_CPPFLAGS += -I$(builddir)/../../includes/sys
AM_CPPFLAGS += -DBINDIR='"$(bindir)"' -DSYSCONFDIR='"$(sysconfdir)"' -DPKGDATADIR='"${pkgdatadir}"' -DPKGLIBDIR='"$(pkglibexecdir)"' -DLOCALEDIR='"$(localedir)"'

AM_CFLAGS += @PIECFLAGS@
AM_CXXFLAGS += @PIECXXFLAGS@
AM_LDFLAGS = @PIELDFLAGS@

bin_PROGRAMS = staprun stap-merge stapsh
pkglibexec_PROGRAMS = stapio

staprun_SOURCES = staprun.c staprun_funcs.c ctl.c common.c ../../privilege.cxx ../../util.cxx
staprun_CFLAGS = $(AM_CFLAGS) -DSINGLE_THREADED
staprun_CXXFLAGS = $(AM_CXXFLAGS) -DSINGLE_THREADED
staprun_LDADD = $(staprun_LIBS)

if HAVE_NSS
staprun_SOURCES += modverify.c ../../nsscommon.cxx
staprun_CFLAGS += $(nss_CFLAGS)
staprun_CXXFLAGS += $(nss_CFLAGS)
staprun_LDADD += $(nss_LIBS)
endif

stapio_SOURCES = stapio.c mainloop.c common.c ctl.c relay.c relay_old.c
stapio_LDADD = -lpthread

man_MANS = staprun.8

stap_merge_SOURCES = stap_merge.c
stap_merge_CFLAGS = $(AM_CFLAGS)
stap_merge_LDFLAGS = $(AM_LDFLAGS)
stap_merge_LDADD =

stapsh_SOURCES = stapsh.c
stapsh_CFLAGS = $(AM_CFLAGS)
stapsh_LDFLAGS = $(AM_LDFLAGS)
stapsh_LDADD =

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
GIT_VERSION_CMD = $(SHELL) $(top_srcdir)/../../git_version.sh
git_version.stamp:
	@if test -f "$(srcdir)/git_version.h"; then \
		if test -f "git_version.h"; then :; \
		else \
			cp "$(srcdir)/git_version.h" "git_version.h"; \
		fi; \
	fi
	$(GIT_VERSION_CMD) -k -s $(top_srcdir)/../.. -o git_version.h
	@if test -s "$(srcdir)/git_version.h"; then \
		if cmp "$(srcdir)/git_version.h" "git_version.h"; then :; \
		else \
			echo "Error: $(srcdir)/git_version.h and git_version.h differ."; \
			echo "       You probably want to remove the former."; \
			exit 1; \
		fi; \
	fi

git_version.h:
	$(srcdir)/git_version.sh -k --srcdir $(srcdir)/../.. -o git_version.h


# Why the "id -u" condition?  This way, an unprivileged user can run
# make install, and have "sudo stap ...." or "sudo staprun ...." work later.
install-exec-hook:
	if [ `id -u` -eq 0 ]; then \
		getent group stapusr >/dev/null && chgrp stapusr "$(DESTDIR)$(bindir)/staprun"; \
		chmod 04110 "$(DESTDIR)$(bindir)/staprun"; \
	fi
