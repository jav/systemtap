dnl configure.ac --- autoconf input file for systemtap testsuite
dnl Process this file with autoconf to produce a configure script.

AC_INIT([systemtap], 0.9.5, systemtap@sources.redhat.com, systemtap)

AC_PREREQ(2.59)
AC_CONFIG_AUX_DIR(..)
AM_INIT_AUTOMAKE([dejagnu foreign])
AM_MAINTAINER_MODE

AC_ARG_ENABLE([dejazilla],
  AS_HELP_STRING([--enable-dejazilla@<:@=EMAIL@:>@],
    [enable dejazilla support to automatically email test results to a
     central public collection point (default is disabled).  Optional
     EMAIL overrides the default email address.]))
case "$enable_dejazilla" in
     no) dejazilla= ;;
     yes) dejazilla=dejazilla@elastic.org ;;
     *) dejazilla="$enable_dejazilla" ;;
esac
if test -n "$dejazilla"; then
   AC_MSG_NOTICE([A "make *check" will email results to $dejazilla])
fi
AC_SUBST(dejazilla)

AC_CONFIG_FILES(Makefile)
AC_OUTPUT
