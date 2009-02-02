dnl configure.ac --- autoconf input file for systemtap
dnl Process this file with autoconf to produce a configure script.

AC_INIT([systemtap],   0.8, systemtap@sources.redhat.com, systemtap)
dnl                  ^^^^^ see also NEWS, systemtap.spec, testsuite/configure.ac

AC_PREREQ(2.59)
AM_INIT_AUTOMAKE
AM_MAINTAINER_MODE

AM_PROG_MKDIR_P
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

AC_ARG_ENABLE([pie],
  [AS_HELP_STRING([--disable-pie], [disable position-independent-executable])])
AS_IF([test "x$enable_pie" != xno],[
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
if test "x${have_xmlto}" == "xyes"; then
AC_MSG_CHECKING([for xmlto pdf support])
cat > conftest.$ac_ext << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE book PUBLIC "-//OASIS//DTD DocBook XML V4.1.2//EN"
"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd" []>
   <book id="pdffromxml">
   </book>
EOF
   xmlto pdf conftest.$ac_ext >& /dev/null
   if test $? == 0; then
      have_xmlto_pdf="yes"
      AC_MSG_RESULT([yes])
   else
      AC_MSG_RESULT([no])
   fi
fi
if test "$enable_refdocs" == "yes"; then
   if test "x${have_xmlto}${have_xmlto_pdf}" != "xyesyes"; then
      AC_MSG_ERROR([cannot find proper yxmlto for building reference documentation])
   fi
fi
if test "x${have_xmlto}${have_xmlto_pdf}" == "xyesyes" -a "$enable_refdocs" != "no" -a "${building_docs}" == "yes"; then
   building_refdocs="yes"
fi
AM_CONDITIONAL([BUILD_REFDOCS], [test "$building_refdocs" == "yes"])

dnl Handle the option to build the server setup.
AC_ARG_ENABLE([server],
  AS_HELP_STRING([--enable-server],
    [enable building of stap-server/client (default on if nss etc. found).]),
  [enable_server=$enableval],
  [enable_server="check"])
PKG_CHECK_MODULES([nss],[nss >= 3], have_nss=yes, have_nss=no)
PKG_CHECK_MODULES([nspr],[nspr >= 4], have_nspr=yes, have_nspr=no)
if test "x${have_nss}${have_nspr}" != "xyesyes"; then
  if test "$enable_server" == "yes"; then
    AC_MSG_ERROR([cannot find all libraries for stap-server])
  fi
  if test "$enable_server" == "check"; then
    AC_MSG_WARN([will not build stap-server, cannot find all libraries])
  fi
fi
AM_CONDITIONAL([BUILD_SERVER], [test "x${have_nss}${have_nspr}" == "xyesyes" -a "$enable_server" != "no"])

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


# This procflags business is for staprun/stapio, which need to
# be compiled with the same bitness as the kernel.  On e.g. PPC,
# userspace might be 32-bit default, but staprun needs to be
# 64-bit.  See also bug #4037.

processor=`uname -p`
case "$processor" in
ppc64) PROCFLAGS=-m64 ;;
x86_64) PROCFLAGS=-m64 ;;
*) PROCFLAGS=""
esac
AC_SUBST([PROCFLAGS])

# Use tr1/unordered_map if available
AC_LANG_PUSH(C++)
AC_CHECK_HEADERS([tr1/unordered_map])
AC_LANG_POP(C++)


if test $build_elfutils = yes; then
  case "$with_elfutils" in
  /*) elfutils_srcdir="$with_elfutils" ;;
  *) elfutils_srcdir="../$with_elfutils" ;;
  esac
  AC_MSG_NOTICE([running ${elfutils_srcdir}/configure])
  # Our libdw.so's libebl will look in $ORIGIN/../lib/... but that
  # $ORIGIN is where libdw.so resides, which is not where there is a ../lib.
  # Note that $libdir might be using a quoted use of $exec_prefix or $prefix.
  # So we must make sure to pass those settings to elfutils configure.
  elfutils_rpath="-Wl,--enable-new-dtags,-rpath,${libdir}/${PACKAGE_NAME}"
  here=`pwd`
  (mkdir -p build-elfutils && cd build-elfutils &&
   ${elfutils_srcdir}/configure --enable-libebl-subdir=${PACKAGE_NAME} \
				--includedir="${here}/include-elfutils" \
				--libdir="${here}/lib-elfutils" \
				--exec-prefix="$exec_prefix" \
				--prefix="$prefix" \
				CFLAGS="${CFLAGS/-Wall/}" \
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
dnl Don't use this directly (when not given it is set to NONE).
AC_DEFINE_UNQUOTED(STAP_PREFIX, "$prefix", [configure prefix location])

AC_CONFIG_HEADERS([config.h:config.in])
AC_CONFIG_FILES(Makefile doc/Makefile doc/SystemTap_Tapset_Reference/Makefile stap.1 stapprobes.5 stapfuncs.5 stapvars.5 stapex.5 staprun.8 stap-server.8 man/stapprobes.iosched.5 man/stapprobes.netdev.5 man/stapprobes.nfs.5 man/stapprobes.nfsd.5 man/stapprobes.pagefault.5 man/stapprobes.process.5 man/stapprobes.rpc.5 man/stapprobes.scsi.5 man/stapprobes.signal.5 man/stapprobes.socket.5 man/stapprobes.tcp.5 man/stapprobes.udp.5 initscript/systemtap)
AC_CONFIG_SUBDIRS(testsuite)
AC_CONFIG_FILES([run-stap], [chmod +x run-stap])
AC_OUTPUT

if test "${prefix}" = "/usr/local"; then
   AC_MSG_NOTICE([])
   AC_MSG_NOTICE([For a private or temporary build of systemtap, we recommend])
   AC_MSG_NOTICE([configuring with a prefix.  For example, try])
   AC_MSG_NOTICE([$0 $ac_configure_args --prefix=$HOME/systemtap-${PACKAGE_VERSION}-$$])
   AC_MSG_NOTICE([Running systemtap uninstalled, entirely out of the build tree,])
   AC_MSG_NOTICE([is not supported.])
fi
