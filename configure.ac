dnl configure.ac --- autoconf input file for systemtap
dnl Process this file with autoconf to produce a configure script.

AC_INIT([systemtap], 0.6, systemtap@sources.redhat.com, systemtap)
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

AC_ARG_ENABLE(prologues,
         AC_HELP_STRING([--enable-prologues],[make -P prologue-searching default]),
	 [
if test "$enable_prologues" = yes; then
  AC_DEFINE([ENABLE_PROLOGUES],[],[make -P prologue-searching default])
fi])


SAVE_LIBS="$LIBS"
AC_CHECK_LIB(sqlite3, sqlite3_open)
sqlite3_LIBS="$LIBS"
AC_SUBST(sqlite3_LIBS)
LIBS="$SAVE_LIBS"

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

AC_CHECK_HEADERS(crash/defs.h,
        build_crashmod=yes,build_crashmod=no,
        [
#define NR_CPUS 256
])
AM_CONDITIONAL(BUILD_CRASHMOD, test $build_crashmod = yes)

AC_CHECK_HEADERS([sys/capability.h], , 
	[AC_MSG_ERROR([cannot find required libcap header (libcap-devel may need to be installed)])])

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

