dnl configure.ac --- autoconf input file for systemtap runtime tools
dnl Process this file with autoconf to produce a configure script.

dnl This line must match ../../configure.ac exactly.
AC_INIT([systemtap], 1.7, systemtap@sourceware.org, systemtap)

AC_PREREQ(2.63)
dnl We don't maintain a ChangeLog, which makes us non-GNU -> foreign.
AM_INIT_AUTOMAKE([no-dist foreign])
AM_MAINTAINER_MODE

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_PROG_CXX
AM_PROG_CC_STDC
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_SYS_LARGEFILE

# Compiling without fPIE by default (see PR 9922)
AC_ARG_ENABLE([pie],
  [AS_HELP_STRING([--enable-pie], [enable position-independent-executable])])
AS_IF([test "x$enable_pie" = xyes],[
  PIECFLAGS='-fPIE'
  PIECXXFLAGS='-fPIE'
  PIELDFLAGS='-pie -Wl,-z,relro -Wl,-z,now'
  save_CFLAGS="$CFLAGS"
  save_CXXFLAGS="$CXXFLAGS"
  save_LDFLAGS="$LDFLAGS"
  CFLAGS="$CFLAGS $PIECFLAGS"
  CXXFLAGS="$CXXFLAGS $PIECXXFLAGS"
  LDFLAGS="$LDFLAGS $PIELDFLAGS"
  AC_LINK_IFELSE([AC_LANG_SOURCE([int main () { return 0; }])], [
     AC_MSG_NOTICE([Compiling with gcc pie et al.])
  ], [
     AC_MSG_NOTICE([Compiler does not support -pie et al.])
     PIECFLAGS=
     PIECXXFLAGS=
     PIELDFLAGS=
  ])
  CFLAGS="$save_CFLAGS"
  CXXFLAGS="$save_CXXFLAGS"
  LDFLAGS="$save_LDFLAGS"
])
AC_SUBST(PIECFLAGS)
AC_SUBST(PIECXXFLAGS)
AC_SUBST(PIELDFLAGS)
AC_SUBST(LOCALEDIR, "$localedir")

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
AC_ARG_ENABLE([nls], [AS_HELP_STRING([--enable-nls], [enable translating program messages])])
AS_IF([test "x$enable_nls" != xno], [
AC_DEFINE([ENABLE_NLS], [1], [Define to 1 if program messages should be translated.])
])


dnl We link only against the system elfutils.  Connecting to a
dnl bundled elfutils build (stap/configure --with-elfutils=PATH)
dnl is too tricky to bother with.

AC_CHECK_HEADERS([libelf.h])

save_LIBS="$LIBS"
dnl this will only succeed with elfutils 0.142+
AC_CHECK_LIB(elf,elf_getshdrstrndx,[
  AC_DEFINE([HAVE_ELF_GETSHDRSTRNDX],[1],[Define to 1 if libelf has elf_getshdrstrndx])
  staprun_LIBS="$staprun_LIBS -lelf"
])
LIBS="$save_LIBS"  

AC_SUBST(staprun_LIBS)
AC_MSG_NOTICE([staprun will link $staprun_LIBS])

AC_CONFIG_HEADERS([config.h:config.in])
AC_CONFIG_FILES(Makefile)
AC_CONFIG_FILES([run-staprun], [chmod +x run-staprun])
AC_OUTPUT
