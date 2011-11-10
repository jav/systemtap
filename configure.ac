dnl configure.ac --- autoconf input file for systemtap
dnl Process this file with autoconf to produce a configure script.

AC_INIT([systemtap], 1.7, systemtap@sourceware.org, systemtap)
dnl                  ^^^ see also NEWS, systemtap.spec, testsuite/configure.ac,
dnl                      runtime/staprun/configure.ac

AC_PREREQ(2.59)
AM_INIT_AUTOMAKE([no-dist foreign])
AM_MAINTAINER_MODE

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_USE_SYSTEM_EXTENSIONS
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
AM_GNU_GETTEXT(external)
AM_GNU_GETTEXT_VERSION(0.17)
AC_CHECK_FUNCS(ppoll)

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

dnl Handle the disable-sdt-probes option.
dnl
dnl Default to --disable-sdt-probes if --enable-sdt-probes/--disable-prologues
dnl was not specified and the gcc version is less than version 4,

if test "${enable_sdt_probes+set}" != set; then
  AC_MSG_CHECKING([to see if sdt probes should be the default])
  if { echo '#if __GNUC__ < 4'
       echo ' yes '
       echo '#endif'; } | ${CC} -E - | grep yes > /dev/null; then
    enable_sdt_probes=no
    AC_MSG_RESULT([no])
  else
    AC_MSG_RESULT([yes])
  fi
fi
AC_ARG_ENABLE([sdt-probes],
  [AS_HELP_STRING([--disable-sdt-probes],
                  [disable process.mark probes in stap, staprun, stapio])])
AS_IF([test "x$enable_sdt_probes" != xno], [
  AC_DEFINE([ENABLE_SDT_PROBES], [1],
            [Define to 1 to enable process.mark probes in stap, staprun, stapio.])
])

AC_ARG_ENABLE([ssp],
  [AS_HELP_STRING([--disable-ssp], [disable gcc stack-protector])])
AS_IF([test "x$enable_ssp" != xno],[
   save_CFLAGS="$CFLAGS"
   save_CXXFLAGS="$CXXFLAGS"
   CXXFLAGS="$CXXFLAGS -Werror -fstack-protector-all -D_FORTIFY_SOURCE=2"
   CFLAGS="$CFLAGS -Werror -fstack-protector-all -D_FORTIFY_SOURCE=2"
   AC_COMPILE_IFELSE([AC_LANG_SOURCE([int something ();])], [
       AC_MSG_NOTICE([Compiling with gcc -fstack-protector-all et al.])
	CFLAGS="$save_CFLAGS -fstack-protector-all -D_FORTIFY_SOURCE=2"
	CXXFLAGS="$save_CXXFLAGS -fstack-protector-all -D_FORTIFY_SOURCE=2"],[
       AC_MSG_NOTICE([Compiler does not support -fstack-protector-all et al.])
       CFLAGS="$save_CFLAGS"
       CXXFLAGS="$save_CXXFLAGS"])])


dnl Link with gold if possible
dnl but: https://bugzilla.redhat.com/show_bug.cgi?id=636603
dnl
dnl AC_PATH_PROG(GOLD, [ld.gold], [no])
dnl if test "x$GOLD" != "xno"
dnl then
dnl   mkdir -p Bdir
dnl   ln -sf $GOLD Bdir/ld
dnl   LDFLAGS="$LDFLAGS -B`pwd`/Bdir/"
dnl   AC_MSG_NOTICE([using ld.gold to link])
dnl fi


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
   AC_LINK_IFELSE([AC_LANG_SOURCE([void main () {}])], [
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
publican_brand="common"
AC_ARG_WITH([publican-brand],
  AS_HELP_STRING([--with-publican-brand=BRAND],
    [building publican documentation guides using given brand (defaults to "common").]),
  [publican_brand=$withval],
  [publican_brand="common"])
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

dnl There is a strange bug in older versions of xmlto when generating pdf.
dnl https://bugzilla.redhat.com/show_bug.cgi?id=526273
dnl So make sure to have a chapter title starting with L plus an refentry.
dnl This will make sure the xmlto pdf support test fails on buggy versions.
cat > conftest.xml << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE book PUBLIC "-//OASIS//DTD DocBook XML V4.1.2//EN"
"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd" []>
   <book lang="en">
   <bookinfo><title>foo</title></bookinfo>
   <chapter id="logging.stp"><title>Logging Tapset</title>
   <refentry id="API-log"><refnamediv><refname>log</refname>
   <refpurpose>logging</refpurpose></refnamediv>
   <refsect1><title>Description</title>
   <para>baz</para></refsect1></refentry></chapter>
   </book>
EOF

if test "x${have_xmlto}" == "xyes"; then
AC_MSG_CHECKING([for xmlto --stringparam support])
   xmlto --stringparam man.authors.section.enabled=0 html-nochunks conftest.xml >/dev/null 2>&1
   if test $? == 0; then
      have_xmlto_stringparam="yes"
      AC_MSG_RESULT([yes])
   else
      AC_MSG_RESULT([no])
   fi
   rm -f conftest.html
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
AM_CONDITIONAL([BUILD_PDFREFDOCS], [test "$have_xmlto_pdf" == "yes"])

rm -f conftest.xml

dnl See if we have the nss/nspr headers and libraries
AC_ARG_WITH([nss],
  AS_HELP_STRING([--without-nss],
    [Do not use NSS even if present]))

AS_IF([test "x$with_nss" != "xno"], [
  PKG_CHECK_MODULES([nss], [nss >= 3],
    [have_nss=yes
     AC_DEFINE([HAVE_NSS], [1], [Define to 1 if you have the nss libraries.])
    ], [have_nss=no])
], [have_nss=no])

AM_CONDITIONAL([HAVE_NSS], [test "${have_nss}" = "yes"])

dnl Handle the option to build the compile server.
AC_ARG_ENABLE([server],
  AS_HELP_STRING([--enable-server],
    [enable building of stap-server (default on if nss etc. found).]),
  [enable_server=$enableval],
  [enable_server="check"])

if test "$enable_server" != "no"; then
dnl See if we have enough libraries and tools to build the compile server
  if test "x${have_nss}" != "xyes"; then
    AC_MSG_WARN([will not build systemtap compile server, cannot find nss headers])
  fi		       
fi
AM_CONDITIONAL([BUILD_SERVER], [test "${have_nss}" == "yes" -a "$enable_server" != "no"])

if test "${have_nss}" != "yes"; then
  AC_MSG_WARN([some compile server client functionality will be restricted, cannot find nss development files])
fi		       

dnl See if we have the avahi libraries and headers
AC_ARG_WITH([avahi],
  AS_HELP_STRING([--without-avahi],
    [Do not use Avahi even if present]))

AS_IF([test "x$with_avahi" != "xno"], [
  PKG_CHECK_MODULES([avahi], [avahi-client],
    [have_avahi=yes
     AC_DEFINE([HAVE_AVAHI], [1], [Define to 1 if you have the avahi libraries.])
    ], [have_avahi=no])
], [have_avahi=no])

AM_CONDITIONAL([HAVE_AVAHI], [test "${have_avahi}" = "yes"])

if test "${have_avahi}" != "yes"; then
  AC_MSG_WARN([some compile-server functionality will be restricted, cannot find avahi development files])
fi

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

dnl Look for librpm.
AC_ARG_WITH([rpm],
  [AS_HELP_STRING([--with-rpm],
                  [query rpm database for missing debuginfos])], [], [with_rpm="auto"])
if test "$with_rpm" != "no"; then
  AC_CHECK_LIB(rpm, rpmtsInitIterator, [
                    AC_DEFINE([HAVE_LIBRPM],[1],[have librpm])
                    stap_LIBS="$stap_LIBS -lrpm"
                    have_librpm="yes"], [have_librpm="no"])
  AC_CHECK_LIB(rpmio, rpmFreeCrypto, [
                    AC_DEFINE([HAVE_LIBRPMIO],[1],[have librpmio])
                    stap_LIBS="$stap_LIBS -lrpmio"
                    have_librpmio="yes"], [have_librpmio="no"])
  if test "x$have_librpm" != "xyes" -a "$with_rpm" == "yes"; then
     AC_MSG_ERROR([cannot find librpm])
  fi
  if test "x$have_librpmio" != "xyes" -a "$with_rpm" == "yes"; then
     AC_MSG_WARN([cannot find librpmio])
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

if test $enable_translator = yes; then
  if test $build_elfutils = no; then
    # Need libdwfl-capable recent elfutils http://elfutils.fedorahosted.org/

    # On modern debian/ubuntu, libebl has been merged into libdw
    # http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=457543
    save_LIBS="$LIBS"
    AC_CHECK_LIB(ebl, ebl_get_elfmachine,[ebl_LIBS=-lebl],[ebl_LIBS=])
    LIBS="$save_LIBS"

    save_LIBS="$LIBS"
    AC_CHECK_LIB(dw, dwfl_module_getsym,[],[
      AC_MSG_ERROR([missing elfutils development headers/libraries (install elfutils-devel, libebl-dev, libdw-dev and/or libebl-devel)])],
      [-Wl,--start-group -ldw $ebl_LIBS -Wl,--end-group -lelf])
    AC_CHECK_LIB(dw, dwarf_getcfi_elf,[],[
      AC_MSG_ERROR([elfutils, libdw too old, need 0.142+])],
      [-Wl,--start-group -ldw $ebl_LIBS -Wl,--end-group -lelf])
    stap_LIBS="$stap_LIBS -Wl,--start-group -ldw $ebl_LIBS -Wl,--end-group -lelf"
    LIBS="$save_LIBS"
  else
    # We built our own and stap_LDFLAGS points at the install.
    stap_LIBS="$stap_LIBS -Wl,--start-group -ldw -lebl -Wl,--end-group -lelf"
  fi
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


AC_CACHE_CHECK([for assembler .section "?" flags support], stap_cv_sectionq, [
old_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wa,--fatal-warnings"
AC_COMPILE_IFELSE([AC_LANG_SOURCE([asm(".section .note.foo,\"?\",\"note\"\n"
		       ".byte 1, 2, 3\n"
		       ".previous\n"
		       ".section .text,\"axG\",\"progbits\",foogroup,comdat\n"
		       ".byte 1\n"
		       ".pushsection .note.foo,\"?\",\"note\"\n"
		       ".byte 4, 5, 6\n"
		       ".popsection\n"
		       ".byte 2\n");])],
		  stap_cv_sectionq=yes, stap_cv_sectionq=no)
CFLAGS="$old_CFLAGS"])
AC_SUBST(support_section_question)
support_section_question=0
if test $stap_cv_sectionq = yes; then
  support_section_question=1
fi
AC_CONFIG_FILES([includes/sys/sdt-config.h po/Makefile.in])

if test $build_elfutils = yes -a $enable_translator = yes; then
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
  AC_COMPILE_IFELSE([AC_LANG_SOURCE([int something ();])], [
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
  # Check whether this is a source release, or a source checkout.
  # We need --enable-maintainer-mode if it isn't a source release.
  # elfutils.spec is only distributed in source releases.
  if test -f ${elfutils_srcdir}/elfutils.spec; then
	need_maintainer_option=""
  else
	need_maintainer_option="--enable-maintainer-mode"
  fi
  (mkdir -p build-elfutils && cd build-elfutils &&
   bash ${elfutils_srcdir}/configure --enable-libebl-subdir=${PACKAGE_NAME} \
				--includedir="${here}/include-elfutils" \
				--libdir="${here}/lib-elfutils" \
				--exec-prefix="$exec_prefix" \
				--prefix="$prefix" \
				${need_maintainer_option} \
				CFLAGS="${CFLAGS/-Wall/} $gnu89_inline_flag" \
				LDFLAGS="$LDFLAGS $elfutils_rpath" &&
   if test -f ${elfutils_srcdir}/config/version.h.in; then
        echo Found elfutils/version.h header...
   else
	echo elfutils/version.h header not found, need elfutils 0.142+...
	exit -1
   fi
  )
  if test $? != 0; then exit -1; fi
fi

AC_SUBST(ENABLE_NLS, "$USE_NLS")
AC_SUBST(localedir, "$localedir")
AC_SUBST(LOCALEDIR, "$localedir")

dnl This is here mainly to make sure that configure --prefix=... changes
dnl the config.h files so files depending on it are recompiled
dnl prefix is passed through indirectly in the Makefile.am AM_CPPFLAGS.
dnl Formerly: Don't use this directly (when not given it is set to NONE).
dnl Currently: inline autoconf's later defaulting

stap_prefix=$prefix
test "$stap_prefix" = NONE && stap_prefix=$ac_default_prefix
AC_DEFINE_UNQUOTED(STAP_PREFIX, "$stap_prefix", [configure prefix location])
AC_SUBST(STAP_PREFIX, "$stap_prefix")

AC_CONFIG_HEADERS([config.h:config.in])
AC_CONFIG_FILES([Makefile doc/Makefile \
doc/beginners/Makefile doc/SystemTap_Tapset_Reference/Makefile \
grapher/Makefile \
grapher/stapgraph.1 \
stappaths.7 \
initscript/systemtap initscript/stap-server ])
AC_CONFIG_SUBDIRS(testsuite runtime/staprun)
if test $enable_translator == "yes"; then
	AC_CONFIG_FILES([run-stap], [chmod +x run-stap])
fi
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
