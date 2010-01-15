dnl configure.ac --- autoconf input file for systemtap
dnl Process this file with autoconf to produce a configure script.

AC_INIT([systemtap], 1.1, systemtap@sources.redhat.com, systemtap)
dnl                  ^^^ see also NEWS, systemtap.spec, testsuite/configure.ac

AC_PREREQ(2.59)
AM_INIT_AUTOMAKE
AM_MAINTAINER_MODE

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AM_PROG_MKDIR_P
AC_SUBST(MKDIR_P)
AC_PROG_LN_S
AC_PROG_CC
AC_PROG_CXX
AC_PROG_CPP
AM_PROG_CC_STDC
AM_PROG_CC_C_O
AM_C_PROTOTYPES
AC_PROG_RANLIB
AC_OBJEXT
AC_EXEEXT
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_SUBST(CFLAGS)
AC_SUBST(CXXFLAGS)

dnl Handle the perfmon option.
AC_ARG_ENABLE([perfmon],
  AS_HELP_STRING([--enable-perfmon@<:@=DIRECTORY@:>@],
    [enable perfmon support (default is disabled).  Optional DIRECTORY
    is the path to the perfmon installation (needed if installed in a
    non-standard location).]), [
  if test "$enable_perfmon" != "no"; then
    dnl Add perfmon define and library
    CPPFLAGS="$CPPFLAGS -DPERFMON"
    LIBS="$LIBS -lpfm"
    dnl Handle custom install dir (if needed)
    if test "$enable_perfmon" != "yes"; then
      CPPFLAGS="$CPPFLAGS -I$enable_perfmon/include"
      LDFLAGS="$LDFLAGS -L$enable_perfmon/lib"
    fi
    AC_CHECK_LIB([pfm], [pfm_start], [], [
      AC_MSG_ERROR([systemtap cannot find required perfmon libs (libpfm-devel may need to be installed])])
  fi])

dnl Handle the prologues option.
dnl
dnl If the user didn't specify --enable-prologues/--disable-prologues
dnl and the x86 system has a version of gcc less than version 4,
dnl automatically enable prologues.
if test "${enable_prologues+set}" != set; then
  AC_MSG_CHECKING([to see if prologue searching should be the default])
  if { echo '#if __i386__ == 1 && __GNUC__ < 4'
       echo ' yes '
       echo '#endif'; } | ${CC} -E - | grep yes > /dev/null; then
    enable_prologues=yes
    AC_MSG_RESULT([yes])
  else
    AC_MSG_RESULT([no])
  fi
fi
AC_ARG_ENABLE([prologues],
  AS_HELP_STRING([--enable-prologues], [make -P prologue-searching default]),
  [
if test "$enable_prologues" = yes; then
  AC_DEFINE([ENABLE_PROLOGUES],[],[make -P prologue-searching default])
fi])

AC_ARG_ENABLE([ssp],
  [AS_HELP_STRING([--disable-ssp], [disable gcc stack-protector])])
AS_IF([test "x$enable_ssp" != xno],[
   save_CFLAGS="$CFLAGS"
   save_CXXFLAGS="$CXXFLAGS"
   CXXFLAGS="$CXXFLAGS -Werror -fstack-protector-all -D_FORTIFY_SOURCE=2"
   CFLAGS="$CFLAGS -Werror -fstack-protector-all -D_FORTIFY_SOURCE=2"
   AC_COMPILE_IFELSE([int something ();], [
       AC_MSG_NOTICE([Compiling with gcc -fstack-protector-all et al.])
	CFLAGS="$save_CFLAGS -fstack-protector-all -D_FORTIFY_SOURCE=2"
	CXFXLAGS="$save_CXXFLAGS -fstack-protector-all -D_FORTIFY_SOURCE=2"],[
       AC_MSG_NOTICE([Compiler does not support -fstack-protector-all et al.])
       CFLAGS="$save_CFLAGS"
       CXXFLAGS="$save_CXXFLAGS"])])

# Compiling without fPIE by default (see PR 9922)
AC_ARG_ENABLE([pie],
  [AS_HELP_STRING([--enable-pie], [enable position-independent-executable])])
AS_IF([test "x$enable_pie" == xyes],[
   save_CFLAGS="$CFLAGS"
   save_CXXFLAGS="$CXXFLAGS"
   save_LDFLAGS="$LDFLAGS"
   CFLAGS="$CFLAGS -fPIE"
   CXXFLAGS="$CXXFLAGS -fPIE"
   LDFLAGS="$LDFLAGS -pie -Wl,-z,relro -Wl,-z,now"
   AC_LINK_IFELSE([void main () {}], [
       AC_MSG_NOTICE([Compiling with gcc pie et al.])
          # LDFLAGS is special since it may be passed down to bundled-elfutils,
          # and interfere with the .so's built therein
          PIELDFLAGS="$LDFLAGS"
          LDFLAGS="$save_LDFLAGS"
          PIECFLAGS="$CFLAGS"
          CFLAGS="$save_CFLAGS"
          PIECXXFLAGS="$CXXFLAGS"
          CXXFLAGS="$save_CXXFLAGS"
       ],[
       AC_MSG_NOTICE([Compiler does not support -pie et al.])
       PIECFLAGS=""
       CFLAGS="$save_CFLAGS"
       PIECXXFLAGS=""
       CXXFLAGS="$save_CXXFLAGS"
       PIELDFLAGS=""
       LDFLAGS="$save_LDFLAGS"])])
AC_SUBST(PIELDFLAGS)
AC_SUBST(PIECFLAGS)
AC_SUBST(PIECXXFLAGS)

dnl Handle optional sqlite support.  If enabled/disabled by the user,
dnl do the right thing.  If not specified by the user, use it if
dnl present.
AC_ARG_ENABLE([sqlite],
  AS_HELP_STRING([--enable-sqlite], [build with sqlite support]),
  [],					dnl ACTION-IF-GIVEN
  [enable_sqlite=check])		dnl ACTION-IF-NOT-GIVEN
sqlite3_LIBS=
AS_IF([test "x$enable_sqlite" != xno],
  [AC_CHECK_LIB([sqlite3], [sqlite3_open],
    [AC_SUBST([sqlite3_LIBS], [-lsqlite3])
     AC_DEFINE([HAVE_LIBSQLITE3], [1], [Define to 1 if you have the `sqlite3' library (-lsqlite3).])],
    [if test "x$enable_sqlite" != xcheck; then
      AC_MSG_FAILURE([--enable-sqlite was given, but test for sqlite failed])
     fi])])

dnl Handle the option to only build runtime
AC_ARG_ENABLE([translator],
  AS_HELP_STRING([--disable-translator], [build only runtime utilities]),
  [],
  [enable_translator="yes"])
AM_CONDITIONAL([BUILD_TRANSLATOR], [test "$enable_translator" == "yes"])

dnl Handle the option to build the crash extension
AC_ARG_ENABLE([crash],
  AS_HELP_STRING([--enable-crash@<:@=DIRECTORY@:>@],
    [enable crash extension (default is disabled).  Optional DIRECTORY
    is the path to the crash header file (needed if installed in a
    non-standard location).]),
  [if test "$enable_crash" != "no"; then
    dnl Handle custom install dir (if needed)
    save_CPPFLAGS="$CPPFLAGS"
    if test "$enable_crash" != "yes"; then
      staplog_CPPFLAGS=-I$enable_crash
      CPPFLAGS="${staplog_CPPFLAGS} $CPPFLAGS"
      AC_SUBST([staplog_CPPFLAGS])
    fi
    AC_CHECK_HEADERS([crash/defs.h], [],
     [AC_MSG_ERROR([cannot find required crash header (crash-devel may need to be installed)])],
     [
#define NR_CPUS 256
     ])
    CPPFLAGS="$save_CPPFLAGS"
  fi],
  [enable_crash="no"])
AM_CONDITIONAL([BUILD_CRASHMOD], [test "$enable_crash" != "no"])

dnl Handle the option to build the documentation
building_docs="no"
AC_ARG_ENABLE([docs],
  AS_HELP_STRING([--enable-docs],
    [enable building documentation (default on if latex etc. found).]),
  [enable_docs=$enableval],
  [enable_docs="check"])
AC_CHECK_PROG(have_latex, latex, yes, no)
AC_CHECK_PROG(have_dvips, dvips, yes, no)
AC_CHECK_PROG(have_ps2pdf, ps2pdf, yes, no)
AC_CHECK_PROG(have_latex2html, latex2html, yes, no)
if test "x${have_latex}${have_dvips}${have_ps2pdf}${have_latex2html}" != "xyesyesyesyes"; then
   if test "$enable_docs" == "yes"; then
      AC_MSG_ERROR([cannot find all tools for building documentation])
   fi
  if test "$enable_docs" == "check"; then
    AC_MSG_WARN([will not build documentation, cannot find all tools])
  fi
fi
if test "x${have_latex}${have_dvips}${have_ps2pdf}${have_latex2html}" == "xyesyesyesyes" -a "$enable_docs" != "no"; then
   building_docs="yes"
fi
AM_CONDITIONAL([BUILD_DOCS], [test "$building_docs" == "yes"])

dnl Handle the options to build the publican options
building_publican="no"
AC_ARG_ENABLE([publican],
  AS_HELP_STRING([--enable-publican],
    [enable building publican documentation guides (default on if publican found and other documentation built).]),
  [enable_publican=$enableval],
  [enable_publican="check"])
if test "$building_docs" == "no" -a "$enable_publican" == "yes" ; then
   AC_MSG_ERROR([must use --enable-docs with --enable-publican])
fi
AC_CHECK_PROG(have_publican, publican, yes, no)
if test "$enable_publican" == "yes"; then
   if test "x${have_publican}" != "xyes"; then
      AC_MSG_ERROR([cannot find publican for building publican guides])
   fi
fi
if test "x${have_publican}" == "xyes" -a "$enable_publican" != "no" -a "${building_docs}" == "yes"; then
   building_publican="yes"
fi
AM_CONDITIONAL([BUILD_PUBLICAN], [test "$building_publican" == "yes"])
publican_brand="fedora"
AC_ARG_WITH([publican-brand],
  AS_HELP_STRING([--with-publican-brand=BRAND],
    [building publican documentation guides using given brand (defaults to "fedora").]),
  [publican_brand=$withval],
  [publican_brand="fedora"])
PUBLICAN_BRAND=$publican_brand
AC_SUBST(PUBLICAN_BRAND)

dnl Handle the option to build the reference documentation
building_refdocs="no"
AC_ARG_ENABLE([refdocs],
  AS_HELP_STRING([--enable-refdocs],
    [enable building reference documentation (default on if xmlto etc. found and other documentation built).]),
  [enable_refdocs=$enableval],
  [enable_refdocs="check"])
if test "$building_docs" == "no" -a "$enable_refdocs" == "yes" ; then
   AC_MSG_ERROR([must use --enable-docs with --enable-refdocs])
fi
AC_CHECK_PROG(have_xmlto, xmlto, yes, no)
if test "$enable_refdocs" == "yes"; then
   if test "x${have_xmlto}" != "xyes"; then
      AC_MSG_ERROR([cannot find xmlto for building reference documentation])
   fi
fi
if test "x${have_xmlto}" == "xyes" -a "$enable_refdocs" != "no" -a "${building_docs}" == "yes"; then
   building_refdocs="yes"
fi
AM_CONDITIONAL([BUILD_REFDOCS], [test "$building_refdocs" == "yes"])

cat > conftest.xml << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE book PUBLIC "-//OASIS//DTD DocBook XML V4.1.2//EN"
"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd" []>
   <book lang="en">
   <bookinfo><title>foo</title></bookinfo>
   <chapter><title>bar</title><para>baz</para></chapter>
   </book>
EOF

if test "x${have_xmlto}" == "xyes"; then
AC_MSG_CHECKING([for xmlto --stringparam support])
   xmlto --stringparam man.authors.section.enabled=0 txt conftest.xml >/dev/null 2>&1
   if test $? == 0; then
      have_xmlto_stringparam="yes"
      AC_MSG_RESULT([yes])
   else
      AC_MSG_RESULT([no])
   fi
   rm -f conftest.txt
fi
AM_CONDITIONAL([XMLTO_STRINGPARAM], [test "$have_xmlto_stringparam" == "yes"])

if test "x${building_refdocs}" == "xyes"; then
AC_MSG_CHECKING([for xmlto pdf support])
   xmlto pdf conftest.xml >& /dev/null
   if test $? == 0; then
      have_xmlto_pdf="yes"
      AC_MSG_RESULT([yes])
   else
      AC_MSG_RESULT([no])
      AC_MSG_WARN([Not building reference documentation in PDF format])
   fi
   rm -f conftest.pdf
fi
dnl Unfortunately https://bugzilla.redhat.com/show_bug.cgi?id=526273 
dnl blocks "xmlto pdf" on our docs (but not on the above test).
dnl So we need to disable this anyway.
dnl AM_CONDITIONAL([BUILD_PDFREFDOCS], [test "$have_xmlto_pdf" == "yes"])
AM_CONDITIONAL([BUILD_PDFREFDOCS], [test "maybe later" == "yes"])

rm -f conftest.xml


dnl Handle the option to build the server setup.
AC_ARG_ENABLE([server],
  AS_HELP_STRING([--enable-server],
    [enable building of stap-server/client (default on if nss etc. found).]),
  [enable_server=$enableval],
  [enable_server="check"])
dnl Find the location of nss and nspr headers and certutil
AC_CHECK_PROG(have_certutil, certutil, [yes], [no])
AC_CHECK_FILE([/usr/include/nss3], nssdir=nss3, [
	AC_CHECK_FILE([/usr/include/nss], nssdir=nss)
])
AC_CHECK_FILE([/usr/include/nspr4], nsprdir=nspr4, [
	AC_CHECK_FILE([/usr/include/nspr], nsprdir=nspr)
])
if test "x$nssdir" != "x" -a "x$nsprdir" != "x"; then
nss_CFLAGS="-I/usr/include/$nssdir"
nspr_CFLAGS="-I/usr/include/$nsprdir"
AC_SUBST(nss_CFLAGS)
AC_SUBST(nspr_CFLAGS)
save_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CFLAGS $nss_CFLAGS $nspr_CFLAGS"
have_nss_includes=yes
AC_CHECK_HEADERS(["$nsprdir/nspr.h" "$nsprdir/plgetopt.h" "$nsprdir/prerror.h" "$nssdir/ssl.h" "$nssdir/nss.h" "$nssdir/pk11func.h" "$nssdir/secerr.h"], [], have_nss_includes=no)
CPPFLAGS="$save_CPPFLAGS"
have_nss_libs=no
AC_CHECK_LIB(nspr4, PR_Connect, [
	AC_CHECK_LIB(ssl3, SSL_ReHandshake, have_nss_libs=yes)
])
fi
if test "x${have_nss_includes}${have_nss_libs}${have_certutil}" != "xyesyesyes"; then
  if test "$enable_server" == "yes"; then
    AC_MSG_ERROR([cannot find all libraries or tools for stap-server])
  fi
  if test "$enable_server" == "check"; then
    AC_MSG_WARN([will not build stap-server, cannot find all libraries or tools])
  fi
else
    AC_DEFINE([HAVE_NSS], [1], [Define to 1 if you have the nss libraries.])
fi
AM_CONDITIONAL([BUILD_SERVER], [test "x${have_nss_includes}${have_nss_libs}${have_certutil}" == "xyesyesyes" -a "$enable_server" != "no"])
AM_CONDITIONAL([HAVE_NSS],     [test "x${have_nss_includes}${have_nss_libs}${have_certutil}" == "xyesyesyes"])

dnl Handle the optional grapher
AC_ARG_ENABLE([grapher],
  AS_HELP_STRING([--enable-grapher],
    [enable building of grapher (default on if needed requirements are found).]),
  [],					dnl ACTION-IF-GIVEN
  [enable_grapher="check"])		dnl ACTION-IF-NOT-GIVEN
dnl If the user didn't disable the grapher, check the grapher's requirements.
AS_IF([test "x$enable_grapher" != xno],
  [
   dnl Check for gtkmm.  Strictly speaking, this isn't necessary,
   dnl since the libglademm24-devel rpm requires the gtkmm24-devel
   dnl rpm.  But, this could be a non-rpm system.  This check will
   dnl give the user a better idea about how to fix missing
   dnl requirements.
   PKG_CHECK_MODULES([gtkmm], [gtkmm-2.4 >= 2.8.0],
     have_gtkmm="yes", have_gtkmm="no")
   AS_IF([test "x${have_gtkmm}" == "xno" -a x"$enable_grapher" == "xyes"],
     AC_MSG_WARN([systemtap cannot find required gtkmm libs (gtkmm24-devel version 2.8.0 or higher may need to be installed)]))   

   dnl Check for libglade
   PKG_CHECK_MODULES([libglade], [libglademm-2.4 >= 2.6.7],
     have_libglade="yes", have_libglade="no")
   AS_IF([test "x${have_libglade}" == "xno" -a x"$enable_grapher" == "xyes"],
     AC_MSG_WARN([systemtap cannot find required libglade libs (libglademm24-devel version 2.6.7 or higher may need to be installed)]))   

   dnl Check for needed boost headers
   have_boost_headers="yes"
   AC_LANG_PUSH(C++)
   AC_CHECK_HEADERS([boost/algorithm/string.hpp boost/range.hpp],
     [], have_boost_headers="no")
   AC_LANG_POP(C++)
   AS_IF([test "x${have_boost_headers}" == "xno" -a x"$enable_grapher" == "xyes"],
     AC_MSG_WARN([cannot find required boost headers (boost-devel may need to be installed)]))

   dnl Bundle up all the grapher requirements into 1 variable.
   AS_IF([test "x${have_gtkmm}${have_libglade}${have_boost_headers}" == "xyesyesyes"],
         have_grapher_reqs="yes", have_grapher_reqs="no")
   ])
dnl If the user explicitly said to enable the grapher, but we can't
dnl (since one or more of its requirements aren't installed), error.
AS_IF([test "x${have_grapher_reqs}" == "xno" -a x"$enable_grapher" == "xyes"],
  AC_MSG_ERROR([systemtap cannot find grapher build requirements (gtkmm, libglade, and boost headers)]))
dnl If the user didn't say to disable the grapher, and all the
dnl requirements are installed, enable the grapher.
AM_CONDITIONAL([BUILD_GRAPHER], [test "x${have_grapher_reqs}" == "xyes" -a x"$enable_grapher" != "xno"])


# Integration with rpm library to support missing debuginfo suggestions.
# --without-rpm: Disable any rpm support.
# --with-rpm=libname.so: Try to dynamically open `libname.so' during runtime.
#   Even with runtime missing `libname.so' GDB will still other run correctly.
#   Missing `libname.so' during ./configure will abort the configuration.
# --with-rpm=librpm.so: Like `--with-rpm=libname.so' but try to find specific
#   minor version first such as `librpm-4.6.so' as minor version differences
#   mean API+ABI incompatibility.  If the specific match versioned library name
#   could not be found still open dynamically at least `librpm.so'.
# --with-rpm: Like `--with-rpm=librpm.so' but if any of its detection fails try
#   to find librpm for compilation-time linking by pkg-config.  GDB binary will
#   be probably linked with the version specific library (as `librpm-4.6.so').
#   Failure to find librpm by pkg-config will abort the configuration.
# (default) --with-rpm=auto: Like `--with-rpm=librpm.so' but if even pkg-config
#   cannot find librpm use to the rpmless compilation (like `--without-rpm').

AC_ARG_WITH([rpm],
  [AS_HELP_STRING([--with-rpm],
                  [query rpm database for missing debuginfos @<:@yes/no, def. auto=librpm.so@:>@])], [], [with_rpm="auto"])

m4_pattern_allow([^AC_MSG_ERROR$])
m4_pattern_allow([^AC_MSG_WARN$])
if test "x$with_rpm" != "xno"; then
  if test "x$with_rpm" = "xyes"; then
    LIBRPM="librpm.so"
    RPM_REQUIRE=true
    DLOPEN_REQUIRE=false
  elif test "x$with_rpm" = "xauto"; then
    LIBRPM="librpm.so"
    RPM_REQUIRE=false
    DLOPEN_REQUIRE=false
  else
    LIBRPM="$with_rpm"
    RPM_REQUIRE=true
    DLOPEN_REQUIRE=true
  fi
  LIBRPM_STRING='"'"$LIBRPM"'"'

  AC_MSG_CHECKING([specific librpm version])
  HAVE_DLOPEN_LIBRPM=false
  save_LIBS="$LIBS"
  LIBS="$LIBS -ldl"
  AC_RUN_IFELSE(AC_LANG_PROGRAM([[
#include <rpm/rpmlib.h>
#include <dlfcn.h>
#include <errno.h>
  ]], [[
    void *h;
    const char *const *rpmverp;
    FILE *f;

    f = fopen ("conftest.out", "w");
    if (!f)
      {
	fprintf (stderr, "Cannot write \"%s\": %s\n", "conftest.out",
		 strerror (errno));
	return 1;
      }
    h = dlopen ($LIBRPM_STRING, RTLD_LAZY);
    if (!h)
      {
	fprintf (stderr, "dlopen (\"%s\"): %s\n", $LIBRPM_STRING, dlerror ());
	return 1;
      }
    rpmverp = dlsym (h, "RPMVERSION");
    if (!rpmverp)
      {
	fprintf (stderr, "dlsym (\"RPMVERSION\"): %s\n", dlerror ());
	return 1;
      }
    fprintf (stderr, "RPMVERSION is: \"");
    fprintf (stderr, "%s\"\n", *rpmverp);

    /* Try to find the specific librpm version only for "librpm.so" as we do
       not know how to assemble the version string otherwise.  */

    if (strcmp ("librpm.so", $LIBRPM_STRING) != 0)
      {
	fprintf (f, "%s\n", $LIBRPM_STRING);
	return 0;
      }
    else
      {
	char *h2_name;
	void *h2;
	int major, minor;

	if (sscanf (*rpmverp, "%d.%d", &major, &minor) != 2)
	  {
	    fprintf (stderr, "Unable to parse RPMVERSION.\n");
	    fprintf (f, "%s\n", $LIBRPM_STRING);
	    return 0;
	  }
	/* Avoid the square brackets by malloc.  */
	h2_name = malloc (64);
	sprintf (h2_name, "librpm-%d.%d.so", major, minor);
	h2 = dlopen (h2_name, RTLD_LAZY);
	if (!h2)
	  {
	    fprintf (stderr, "dlopen (\"%s\"): %s\n", h2_name, dlerror ());
	    fprintf (f, "%s\n", $LIBRPM_STRING);
	    return 0;
	  }
	if (h2 != h)
	  {
	    fprintf (stderr, "dlopen of \"%s\" and \"%s\" are different.\n",
		     $LIBRPM_STRING, h2_name);
	    fprintf (f, "%s\n", $LIBRPM_STRING);
	    return 0;
	  }
	/* Found the valid .so name with a specific version.  */
	fprintf (f, "%s\n", h2_name);
	return 0;
      }
  ]]), [
    DLOPEN_LIBRPM="`cat conftest.out`"
    if test "x$DLOPEN_LIBRPM" != "x"; then
      HAVE_DLOPEN_LIBRPM=true
      AC_MSG_RESULT($DLOPEN_LIBRPM)
    fi
  ])
  rm -f conftest.out

  m4_define([CHECK_LIBRPM_COMPAT], [
    AC_MSG_CHECKING([rpm library API compatibility])
    # The compilation requires -Werror to verify anything.
    save_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS -Werror"
    AC_COMPILE_IFELSE(AC_LANG_PROGRAM([[
/* Duplicate here the declarations to verify they match "symfile.c".  */
#include <rpm/rpmlib.h>
#include <rpm/rpmts.h>
#include <rpm/rpmdb.h>
#include <rpm/header.h>
extern char * headerFormat(Header h, const char * fmt, errmsg_t * errmsg);
extern int rpmReadConfigFiles(const char * file, const char * target);
extern rpmdbMatchIterator rpmdbFreeIterator(rpmdbMatchIterator mi);
extern Header rpmdbNextIterator(rpmdbMatchIterator mi);
extern rpmts rpmtsCreate(void);
extern rpmts rpmtsFree(rpmts ts);
extern rpmdbMatchIterator rpmtsInitIterator(const rpmts ts, rpmTag rpmtag,
					    const void * keyp, size_t keylen);
    ]]), [
      LIBRPM_COMPAT=true
      rpm_LIBS=-lrpm
      AC_MSG_RESULT(yes)
    ], [
      LIBRPM_COMPAT=false
      rpm_LIBS=
      AC_MSG_RESULT(no)
    ])
    CFLAGS="$save_CFLAGS"
  ])
  AC_SUBST(rpm_LIBS)

  if $HAVE_DLOPEN_LIBRPM; then
    CHECK_LIBRPM_COMPAT
    if ! $LIBRPM_COMPAT; then
      HAVE_DLOPEN_LIBRPM=false
    fi
  fi

  if $HAVE_DLOPEN_LIBRPM; then
    DLOPEN_LIBRPM_STRING='"'"$DLOPEN_LIBRPM"'"'
    AC_DEFINE_UNQUOTED(DLOPEN_LIBRPM, $DLOPEN_LIBRPM_STRING, [librpm version specific library name to dlopen.])
    AC_DEFINE(HAVE_LIBRPM, 1, [Define if librpm library is being used.])
  else
    AC_MSG_RESULT(no)
    LIBS="$save_LIBS"
    if $DLOPEN_REQUIRE; then
      AC_MSG_ERROR([Specific name $LIBRPM was requested but it could not be opened.])
    fi
    PKG_CHECK_MODULES(RPM, rpm, [HAVE_LIBRPM=true], [HAVE_LIBRPM=false])

    if $HAVE_LIBRPM; then
      CHECK_LIBRPM_COMPAT
      if ! $LIBRPM_COMPAT; then
	HAVE_LIBRPM=false
	RPM_PKG_ERRORS="Found $LIBRPM API is incompatibile with this GDB"
      fi
    fi

    if $HAVE_LIBRPM; then
      AC_DEFINE(HAVE_LIBRPM, 1, [Define if librpm library is being used.])
      CFLAGS="$CFLAGS $RPM_CFLAGS"
      LIBS="$LIBS $RPM_LIBS"
    else
      if $RPM_REQUIRE; then
	AC_MSG_ERROR($RPM_PKG_ERRORS)
      else
	AC_MSG_WARN($RPM_PKG_ERRORS)
      fi
    fi
  fi
fi


dnl Handle elfutils.  If '--with-elfutils=DIR' wasn't specified, used
dnl the system's elfutils.
build_elfutils=no
AC_ARG_WITH([elfutils],
  AS_HELP_STRING([--with-elfutils=DIRECTORY],
    [find elfutils source code in DIRECTORY]),
  [
case "$with_elfutils" in
yes) AC_MSG_ERROR([--with-elfutils requires an argument]) ;;
''|no) ;;
*) build_elfutils=yes ;;
esac])
AM_CONDITIONAL(BUILD_ELFUTILS, test $build_elfutils = yes)
AC_SUBST(elfutils_abs_srcdir, `AS_IF([test $build_elfutils = yes],
				     [cd $with_elfutils && pwd])`)

if test $build_elfutils = no; then
  # Need libdwfl-capable recent elfutils http://elfutils.fedorahosted.org/
  save_LIBS="$LIBS"
  AC_CHECK_LIB(dw, dwfl_module_getsym,[],[
    AC_MSG_ERROR([missing elfutils development headers/libraries (install elfutils-devel, libebl-dev, libdw-dev and/or libebl-devel)])],
    [-Wl,--start-group -ldw -lebl -Wl,--end-group -lelf])
  AC_CHECK_LIB(dw, dwarf_getelf,[],[
    AC_MSG_ERROR([elfutils, libdw too old, need 0.126+])],
    [-Wl,--start-group -ldw -lebl -Wl,--end-group -lelf])
  stap_LIBS="-Wl,--start-group -ldw -lebl -Wl,--end-group -lelf"
  LIBS="$save_LIBS"
else
  # We built our own and stap_LDFLAGS points at the install.
  stap_LIBS="-Wl,--start-group -ldw -lebl -Wl,--end-group -lelf"
fi

AC_SUBST(stap_LIBS)
AC_MSG_NOTICE([stap will link $stap_LIBS])

dnl Plop in the build (configure) date
date=`date +%Y-%m-%d`
AC_DEFINE_UNQUOTED(DATE, "$date", [Configuration/build date])
AC_SUBST(DATE, "$date")

# Before PR4037, we used to arrange to pass CFLAGS+=-m64 for a staprun
# being compiled on 32-bit userspace but running against 64-bit kernels.
# This is no longer necessary.

# Use tr1/unordered_map if available
AC_LANG_PUSH(C++)
AC_CHECK_HEADERS([tr1/unordered_map])
AC_CHECK_HEADERS([tr1/memory])
AC_CHECK_HEADERS([boost/shared_ptr.hpp])
AC_LANG_POP(C++)


if test $build_elfutils = yes; then
  case "$with_elfutils" in
  /*) elfutils_srcdir="$with_elfutils" ;;
  *) elfutils_srcdir="../$with_elfutils" ;;
  esac
  AC_MSG_NOTICE([running ${elfutils_srcdir}/configure])

  save_CFLAGS="$CFLAGS"
  save_CXXFLAGS="$CXXFLAGS"
  CXXFLAGS="$CXXFLAGS -fgnu89-inline"
  CFLAGS="$CFLAGS -fgnu89-inline"
  gnu89_inline_flag=""
  AC_COMPILE_IFELSE([int something ();], [
    AC_MSG_NOTICE([Compiling elfutils with gcc -fgnu89-inline])
    gnu89_inline_flag="-fgnu89-inline"],[
    AC_MSG_NOTICE([Compiler does not support -fgnu89-inline])])
  CFLAGS="$save_CFLAGS"
  CXXFLAGS="$save_CXXFLAGS"

  # Our libdw.so's libebl will look in $ORIGIN/../lib/... but that
  # $ORIGIN is where libdw.so resides, which is not where there is a ../lib.
  # Note that $libdir might be using a quoted use of $exec_prefix or $prefix.
  # So we must make sure to pass those settings to elfutils configure.
  elfutils_rpath="-Wl,--enable-new-dtags,-rpath,${libdir}/${PACKAGE_NAME}"
  here=`pwd`
  (mkdir -p build-elfutils && cd build-elfutils &&
   bash ${elfutils_srcdir}/configure --enable-libebl-subdir=${PACKAGE_NAME} \
				--includedir="${here}/include-elfutils" \
				--libdir="${here}/lib-elfutils" \
				--exec-prefix="$exec_prefix" \
				--prefix="$prefix" \
				CFLAGS="${CFLAGS/-Wall/} $gnu89_inline_flag" \
				LDFLAGS="$LDFLAGS $elfutils_rpath" &&
   if test -f ${elfutils_srcdir}/config/version.h.in; then
        echo Trying to install elfutils version header...
	make install-pkgincludeHEADERS
   fi
   # so that elfutils/version.h - available from 0.138 - may be found.
   # Don't use install-data because that will fail (installing in $prefix).
  )
  if test $? != 0; then exit; fi
  save_CPPFLAGS=${CPPFLAGS}
  CPPFLAGS="${CPPFLAGS} -Iinclude-elfutils" # in case bundled elfutils
  AC_CHECK_HEADERS([elfutils/version.h])
  CPPFLAGS="$save_CPPFLAGS"
else
  AC_CHECK_HEADERS([elfutils/version.h])
fi

dnl This is here mainly to make sure that configure --prefix=... changes
dnl the config.h files so files depending on it are recompiled
dnl prefix is passed through indirectly in the Makefile.am AM_CPPFLAGS.
dnl Formerly: Don't use this directly (when not given it is set to NONE).
dnl Currently: inline autoconf's later defaulting
stap_prefix=$prefix
test "$stap_prefix" = NONE && stap_prefix=$ac_default_prefix
AC_DEFINE_UNQUOTED(STAP_PREFIX, "$stap_prefix", [configure prefix location])

AC_CONFIG_HEADERS([config.h:config.in])
AC_CONFIG_FILES(Makefile doc/Makefile doc/beginners/Makefile doc/SystemTap_Tapset_Reference/Makefile grapher/Makefile grapher/stapgraph.1 stap.1 stapprobes.3stap stapfuncs.3stap stapvars.3stap stapex.3stap staprun.8 stap-client.8 stap-server.8 stap-authorize-server-cert.8 stap-authorize-signing-cert.8 man/stapprobes.iosched.3stap man/stapprobes.irq.3stap man/stapprobes.netdev.3stap man/stapprobes.nfs.3stap man/stapprobes.nfsd.3stap man/stapprobes.pagefault.3stap man/stapprobes.kprocess.3stap man/stapprobes.rpc.3stap man/stapprobes.scsi.3stap man/stapprobes.signal.3stap man/stapprobes.socket.3stap man/stapprobes.tcp.3stap man/stapprobes.udp.3stap man/stapprobes.snmp.3stap initscript/systemtap initscript/stap-server)
AC_CONFIG_SUBDIRS(testsuite)
if test $enable_translator == "yes"; then
	AC_CONFIG_FILES([run-stap], [chmod +x run-stap])
fi
AC_CONFIG_FILES([run-staprun], [chmod +x run-staprun])
AC_CONFIG_FILES([dtrace], [chmod +x dtrace])

# Setup "shadow" directory doc/beginners that has everything setup for
# publican in one directory (through directory links if necessary).
# It would be nice to use AC_CONFIG_LINKS, but automake complains when
# the src is a directory and not a file.
AC_CONFIG_FILES([doc/beginners/publican.cfg:doc/SystemTap_Beginners_Guide/publican.cfg.in])
AC_CONFIG_COMMANDS([doc/beginners],
                   [rm -f $ac_abs_top_builddir/doc/beginners/en-US $ac_abs_top_builddir/doc/beginners/build/en-US/testsuite && mkdir -p $ac_abs_top_builddir/doc/beginners/build/en-US && ln -s $ac_abs_top_srcdir/doc/SystemTap_Beginners_Guide/en-US $ac_abs_top_builddir/doc/beginners/en-US && ln -s $ac_abs_top_srcdir/testsuite $ac_abs_top_builddir/doc/beginners/build/en-US/testsuite])

AC_OUTPUT

if test "${prefix}" = "/usr/local"; then
   AC_MSG_NOTICE([])
   AC_MSG_NOTICE([For a private or temporary build of systemtap, we recommend])
   AC_MSG_NOTICE([configuring with a prefix.  For example, try])
   AC_MSG_NOTICE([$0 $ac_configure_args --prefix=$HOME/systemtap-${PACKAGE_VERSION}-$$])
   AC_MSG_NOTICE([Running systemtap uninstalled, entirely out of the build tree,])
   AC_MSG_NOTICE([is not supported.])
fi
