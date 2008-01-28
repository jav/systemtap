dnl configure.ac --- autoconf input file for systemtap
dnl Process this file with autoconf to produce a configure script.

AC_INIT([systemtap], 0.6.1, systemtap@sources.redhat.com, systemtap)
dnl                  ^^^ see also NEWS, testsuite/configure.ac

AC_PREREQ(2.59)
AM_INIT_AUTOMAKE
AM_MAINTAINER_MODE

AC_PROG_MKDIR_P
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
  [AS_HELP_STRING([--enable-ssp], [enable gcc stack-protector])])
AS_IF([test "x$enable_ssp" == xyes],
  [CFLAGS="$CFLAGS -fstack-protector-all -D_FORTIFY_SOURCE=2"
   CXXFLAGS="$CXXFLAGS -fstack-protector-all -D_FORTIFY_SOURCE=2"
   AC_MSG_NOTICE([Compiling with gcc -fstack-protector-all et al.])])

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
     AC_DEFINE([HAVE_LIBSQLITE3])],
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

AC_CHECK_HEADERS([sys/capability.h], , 
  [AC_MSG_ERROR([cannot find required libcap header (libcap-devel may need to be installed)])])

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
  # Need libdwfl-capable recent elfutils from Fedora
  save_LIBS="$LIBS"
  AC_CHECK_LIB(dw, dwfl_module_getsym,,[
    AC_MSG_ERROR([missing elfutils development headers/libraries (dw 0.123+)])])
  AC_CHECK_LIB(ebl, ebl_openbackend,,[
    AC_MSG_ERROR([missing elfutils development headers/libraries (ebl 0.123+)])])

  stap_LIBS="$LIBS"
  LIBS="$save_LIBS"
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

# -lcap is used for staprun, so PROCFLAGS comes in to play
SAVE_LIBS="$LIBS"
SAVE_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS $PROCFLAGS"
AC_CHECK_LIB(cap, cap_init, [], [AC_MSG_ERROR(need $PROCFLAGS -lcap)])
cap_LIBS="$LIBS"
AC_SUBST(cap_LIBS)
LIBS="$SAVE_LIBS"
CFLAGS="$SAVE_CFLAGS"

dnl Create SNAPSHOT file from git commit id if possible
if test -d $srcdir/.git -a ! -f $srcdir/SNAPSHOT; then
 snapshot=`cd $srcdir; git-rev-list --abbrev-commit --max-count=1 HEAD`
 echo $snapshot > SNAPSHOT
 AC_MSG_NOTICE([Created git SNAPSHOT $snapshot]) 
fi

AC_CONFIG_HEADERS([config.h:config.in])
AC_CONFIG_FILES(Makefile systemtap.spec stap.1 stapprobes.5 stapfuncs.5 stapex.5 staprun.8 man/stapprobes.iosched.5 man/stapprobes.netdev.5 man/stapprobes.nfs.5 man/stapprobes.nfsd.5 man/stapprobes.pagefault.5 man/stapprobes.process.5 man/stapprobes.rpc.5 man/stapprobes.scsi.5 man/stapprobes.signal.5 man/stapprobes.socket.5 man/stapprobes.tcp.5 man/stapprobes.udp.5)
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

