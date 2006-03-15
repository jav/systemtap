dnl configure.ac --- autoconf input file for systemtap
dnl Process this file with autoconf to produce a configure script.

AC_INIT([systemtap], 0.5.4, systemtap@sources.redhat.com, systemtap)

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

if test $build_elfutils = no; then
  # Need libdwfl-capable recent elfutils from Fedora
  save_LIBS="$LIBS"
  AC_CHECK_LIB(dw, dwarf_diecu,,[
    AC_MSG_ERROR([systemtap requires elfutils 0.116+])])
  stap_LIBS="$LIBS"
  LIBS="$SAVE_LIBS"
else
  # We built our own and stap_LDFLAGS points at the install.
  stap_LIBS=-ldw
fi
AC_SUBST(stap_LIBS)

dnl Plop in the build (configure) date
date=`date +%Y-%m-%d`
AC_DEFINE_UNQUOTED(DATE, "$date", [Configuration/build date])
AC_SUBST(DATE, "$date")

uname_r=`uname -r`
relayfs_version_ge_4=0
AC_CHECK_FILE(/lib/modules/$uname_r/build/include/linux/relayfs_fs.h,
[
  # grep needs the two tabs in the following line
  if grep "RELAYFS_CHANNEL_VERSION		[[456]]" /lib/modules/$uname_r/build/include/linux/relayfs_fs.h >/dev/null; then
    relayfs_version_ge_4=1
 fi
])
AC_SUBST(RELAYFS_VERSION_GE_4, "$relayfs_version_ge_4")

AC_CONFIG_HEADERS([config.h:config.in])
AC_CONFIG_FILES(Makefile systemtap.spec stp_check stap.1 stapprobes.5 stapfuncs.5 stapex.5 runtime/transport/relayfs-config.h)
AC_OUTPUT

if test $build_elfutils = yes; then
  case "$with_elfutils" in
  /*) elfutils_srcdir="$with_elfutils" ;;
  *) elfutils_srcdir="../$with_elfutils" ;;
  esac
  AC_MSG_NOTICE([running ${elfutils_srcdir}/configure])
  # Our libdw.so's libebl will look in $ORIGIN/../lib/... but that
  # $ORIGIN is where libdw.so resides, which is not where there is a ../lib.
  elfutils_rpath="-Wl,--enable-new-dtags,-rpath,${libdir}/${PACKAGE_NAME}"
  here=`pwd`
  (mkdir -p build-elfutils && cd build-elfutils &&
   ${elfutils_srcdir}/configure --enable-libebl-subdir=${PACKAGE_NAME} \
				--includedir="${here}/include-elfutils" \
				--libdir="${here}/lib-elfutils" \
				LDFLAGS="$LDFLAGS $elfutils_rpath"
  )
fi
