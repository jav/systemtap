dnl #########################################################################
dnl Gets elfutils library version number.
dnl
dnl Result: sets ax_elfutils_get_version_ok to yes or no,
dnl         sets ax_elfutils_get_version_VERSION to version.
dnl
dnl AX_ELFUTILS_GET_VERSION([ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
AC_DEFUN([AX_ELFUTILS_GET_VERSION], [
  dnl # Used to indicate success or failure of this function.
  ax_elfutils_get_version_ok=no
  ax_elfutils_get_version_VERSION=''

  dnl # save and modify environment
  ax_elfutils_get_version_save_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="-D_GNU_SOURCE $CPPFLAGS"

  ax_elfutils_get_version_save_LIBS="$LIBS"
  LIBS="$LIBS -ldw"

  # Compile and run a program that returns the value of the library
  # function dwfl_version.
  AC_RUN_IFELSE([
    AC_LANG_SOURCE([[
#include <stdio.h>
#include <elfutils/libdwfl.h>

int main(int argc,char **argv)
{
  const char *ver = dwfl_version(NULL);
  if (ver != NULL) {
    if (argc > 1)
      printf("%s\n", ver);
    return 0;
  }
  else
    return 1;
}
    ]])
  ],[
    # Program compiled and ran, so get version by adding argument.
    ax_elfutils_get_version_VERSION=`./conftest$ac_exeext x`
    ax_elfutils_get_version_ok=yes
  ],[],[])

  dnl # restore environment
  CPPFLAGS="$ax_elfutils_get_version_save_CPPFLAGS"
  LIBS="$ax_elfutils_get_version_save_LIBS"

  dnl # Finally, execute ACTION-IF-FOUND / ACTION-IF-NOT-FOUND.
  if test "$ax_elfutils_get_version_ok" = "yes" ; then
    AC_MSG_RESULT([$ax_elfutils_get_version_VERSION])
    m4_ifvaln([$1],[$1])dnl
  else
    AC_MSG_RESULT([no])
    m4_ifvaln([$2],[$2])dnl
  fi
]) dnl AX_ELFUTILS_GET_VERSION
