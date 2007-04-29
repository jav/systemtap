dnl configure.ac --- autoconf input file for systemtap
dnl Process this file with autoconf to produce a configure script.

AC_INIT([systemtap], 0.5.14, systemtap@sources.redhat.com, systemtap)
dnl                  ^^^^^^ see also testsuite/configure.ac


AC_PREREQ(2.59)
AM_INIT_AUTOMAKE
AM_MAINTAINER_MODE

AC_PROG_LN_S
AC_PROG_CC
AC_PROG_CXX
AC_PROG_CPP
AM_PROG_CC_STDC
AM_C_PROTOTYPES
AC_LANG_CPLUSPLUS
AC_PROG_RANLIB
AC_OBJEXT
AC_EXEEXT
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_SUBST(CFLAGS)
AC_SUBST(CXXFLAGS)

dnl enable option to generate code for using perfmon
AC_ARG_ENABLE(perfmon,
	[  --enable-perfmon                 enable perfmon support (default is disabled)],
	perfmon_support=$enableval, perfmon_support=no)

AC_ARG_WITH([perfmon],
	     AC_HELP_STRING([--with-perfmon=DIRECTORY],
			    [Path to Perfmon installation to use]),
	    [
case "$with_perfmon" in
yes ) AC_MSG_ERROR([--with-perfmon requires an argument]) ;;
''|no) ;;
*) perfmon_support=yes ;;
esac])


dnl XXX fixme so that it it can use perfmon installed for real
dnl check that perfmon libraries are actually there
if test $perfmon_support = yes; then
  AC_CHECK_LIB(pfm, pfm_start,,[
    AC_MSG_ERROR([systemtap cannot find required perfmon libs])])
  CFLAGS="$CFLAGS -DPERFMON -I$with_perfmon/include"
  CXXFLAGS="$CXXFLAGS -DPERFMON -I$with_perfmon/include"
  LDFLAGS="$LDFLAGS -DPERFMON -L$with_perfmon/lib -lpfm"
fi

AC_ARG_ENABLE(prologues,
         AC_HELP_STRING([--enable-prologues],[make -P prologue-searching default]),
         AC_DEFINE([ENABLE_PROLOGUES],[],[make -P prologue-searching default]))

build_elfutils=no
AC_ARG_WITH([elfutils],
            AC_HELP_STRING([--with-elfutils=DIRECTORY],
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

AM_CONDITIONAL(BUILD_LKET_B2A, pkg-config --atleast-version 2.0.0 glib-2.0)

mysql=false

SAVE_LDFLAGS=$LDFLAGS
SAVE_CPPFLAGS=$CPPFLAGS

AC_PATH_PROG(MYSQL_CONFIG, mysql_config, no)
if test "$MYSQL_CONFIG" = "no"; then
  AC_MSG_NOTICE([*** Cannot find mysql_config executable. Guess MySQL compiling flags.])
  CPPFLAGS="-I/usr/include/mysql $CPPFLAGS"
  LDFLAGS="-L/usr/lib/mysql $LDFLAGS -L/usr/lib64/mysql"
else
  CPPFLAGS="`mysql_config --cflags` $CPPFLAGS"
  LDFLAGS="`mysql_config --libs` $LDFLAGS"
fi

AC_CHECK_HEADERS(mysql/mysql.h,
  AC_CHECK_LIB(mysqlclient, mysql_init,
    mysql=true,
    AC_MSG_NOTICE([*** Cannot find mysqlclient lib. lket-b2a will be built without MySQL support.]), []),
  AC_MSG_NOTICE([*** Cannot find mysql header file. lket-b2a will be built without MySQL support.]), [])

AM_CONDITIONAL(HAS_MYSQL, test "$mysql" = "true")
AM_CONDITIONAL(HAS_MYSQL_CONFIG, test "$MYSQL_CONFIG" != "no")

LDFLAGS=$SAVE_LDFLAGS
CPPFLAGS=$SAVE_CPPFLAGS

if ! pkg-config --atleast-version 2.0.0 glib-2.0; then
  AC_MSG_WARN([glib2-devel is required to build lket-b2a.
  no glib2-devel found, skip building lket-b2a...])
fi

if test $build_elfutils = no; then
  # Need libdwfl-capable recent elfutils from Fedora
  save_LIBS="$LIBS"
  AC_CHECK_LIB(dw, dwfl_module_getsym,,[
    AC_MSG_ERROR([missing elfutils development headers/libraries (dw 0.123+)])])
  AC_CHECK_LIB(ebl, ebl_openbackend,,[
    AC_MSG_ERROR([missing elfutils development headers/libraries (ebl 0.123+)])])
  stap_LIBS="$LIBS"
  LIBS="$SAVE_LIBS"
else
  # We built our own and stap_LDFLAGS points at the install.
  stap_LIBS="-ldw -lebl"
fi
AC_SUBST(stap_LIBS)
AC_MSG_NOTICE([stap will link $stap_LIBS])

dnl Plop in the build (configure) date
date=`date +%Y-%m-%d`
AC_DEFINE_UNQUOTED(DATE, "$date", [Configuration/build date])
AC_SUBST(DATE, "$date")

processor=`uname -p`
case "$processor" in
ppc64) PROCFLAGS=-m64 ;;
x86_64) PROCFLAGS=-m64 ;;
*) PROCFLAGS=""
esac
AC_SUBST([PROCFLAGS])

AC_CONFIG_HEADERS([config.h:config.in])
AC_CONFIG_FILES(Makefile systemtap.spec stp_check stap.1 stapprobes.5 stapfuncs.5 stapex.5 staprun.8 lket.5 lket-b2a.1 runtime/lket/b2a/Makefile man/stapprobes.iosched.5 man/stapprobes.netdev.5 man/stapprobes.nfs.5 man/stapprobes.nfsd.5 man/stapprobes.pagefault.5 man/stapprobes.process.5 man/stapprobes.rpc.5 man/stapprobes.scsi.5 man/stapprobes.signal.5 man/stapprobes.socket.5 man/stapprobes.tcp.5 man/stapprobes.udp.5)
AC_CONFIG_SUBDIRS(testsuite)

AC_OUTPUT

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
				LDFLAGS="$LDFLAGS $elfutils_rpath"
  )
fi

