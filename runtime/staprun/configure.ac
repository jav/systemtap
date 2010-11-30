dnl configure.ac --- autoconf input file for systemtap runtime tools
dnl Process this file with autoconf to produce a configure script.

dnl This line must match ../../configure.ac exactly.
AC_INIT([systemtap], 1.4, systemtap@sources.redhat.com, systemtap)

AC_PREREQ(2.63)
dnl We don't maintain a ChangeLog, which makes us non-GNU -> foreign.
AM_INIT_AUTOMAKE([no-dist foreign])
AM_MAINTAINER_MODE

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_PROG_CC
AM_PROG_CC_STDC
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_SYS_LARGEFILE

# Compiling without fPIE by default (see PR 9922)
AC_ARG_ENABLE([pie],
  [AS_HELP_STRING([--enable-pie], [enable position-independent-executable])])
AS_IF([test "x$enable_pie" = xyes],[
  PIECFLAGS='-fPIE'
  PIELDFLAGS='-pie -Wl,-z,relro -Wl,-z,now'
  save_CFLAGS="$CFLAGS"
  save_LDFLAGS="$LDFLAGS"
  CFLAGS="$CFLAGS $PIECFLAGS"
  LDFLAGS="$LDFLAGS $PIELDFLAGS"
  AC_LINK_IFELSE([int main () { return 0; }], [
     AC_MSG_NOTICE([Compiling with gcc pie et al.])
  ], [
     AC_MSG_NOTICE([Compiler does not support -pie et al.])
     PIECFLAGS=
     PIELDFLAGS=
  ])
  CFLAGS="$save_CFLAGS"
  LDFLAGS="$save_LDFLAGS"
])
AC_SUBST(PIECFLAGS)
AC_SUBST(PIELDFLAGS)

AC_ARG_ENABLE([sdt-probes],
  [AS_HELP_STRING([--disable-sdt-probes],
                  [disable process.mark probes in staprun, stapio])])
AS_IF([test "x$enable_sdt_probes" != xno], [
  AC_DEFINE([ENABLE_SDT_PROBES], [1],
            [Define to 1 to enable process.mark probes in staprun, stapio.])
])

PKG_CHECK_MODULES([nss], [nss >= 3], [have_nss=yes], [have_nss=no])
AM_CONDITIONAL([HAVE_NSS], [test $have_nss = yes])
AS_IF([test $have_nss = yes], [
  AC_DEFINE([HAVE_NSS], [1], [Define to 1 if you have the nss libraries.])
])

AC_CONFIG_HEADERS([config.h:config.in])
AC_CONFIG_FILES(Makefile)
AC_CONFIG_FILES([run-staprun], [chmod +x run-staprun])
AC_OUTPUT
