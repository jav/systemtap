dnl configure.ac --- autoconf input file for systemtap
dnl Process this file with autoconf to produce a configure script.

AC_INIT([systemtap], 0.2.2, systemtap@sources.redhat.com, systemtap)

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

dnl Need libdwfl-capable elfutils 0.111 or later from Fedora
save_LIBS="$LIBS"
AC_CHECK_LIB(dw, dwfl_begin)
stap_LIBS="$LIBS"
LIBS="$SAVE_LIBS"
AC_SUBST(stap_LIBS)

if test $ac_cv_lib_dw_dwfl_begin != yes
then
  AC_MSG_ERROR([systemtap requires elfutils 0.111+])
fi

dnl Plop in the build (configure) date
date=`date +%Y-%m-%d`
AC_DEFINE_UNQUOTED(DATE, "$date", [Configuration/build date])
AC_SUBST(DATE)

AC_CONFIG_HEADERS([config.h:config.in])
AC_CONFIG_FILES(Makefile systemtap.spec stp_check)
AC_OUTPUT
