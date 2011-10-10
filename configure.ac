AC_PREREQ(2.52)
m4_include([m4/c99-backport.m4])
AC_INIT(cacheismo, 0.1, cacheismo@googlegroups.com)
AC_CANONICAL_SYSTEM
AC_CONFIG_SRCDIR(src)
AM_INIT_AUTOMAKE
LT_INIT([dlopen])
AM_CONFIG_HEADER(config.h)
AC_PROG_CC
AC_PROG_INSTALL

dnl **********************************************************************
dnl DETECT_ICC ([ACTION-IF-YES], [ACTION-IF-NO])
dnl
dnl check if this is the Intel ICC compiler, and if so run the ACTION-IF-YES
dnl sets the $ICC variable to "yes" or "no"
dnl **********************************************************************
AC_DEFUN([DETECT_ICC],
[
    ICC="no"
    AC_MSG_CHECKING([for icc in use])
    if test "$GCC" = "yes"; then
       dnl check if this is icc acting as gcc in disguise
       AC_EGREP_CPP([^__INTEL_COMPILER], [__INTEL_COMPILER],
         AC_MSG_RESULT([no])
         [$2],
         AC_MSG_RESULT([yes])
         [$1]
         ICC="yes")
    else
       AC_MSG_RESULT([no])
       [$2]
    fi
])

DETECT_ICC([], [])

dnl **********************************************************************
dnl DETECT_SUNCC ([ACTION-IF-YES], [ACTION-IF-NO])
dnl
dnl check if this is the Sun Studio compiler, and if so run the ACTION-IF-YES
dnl sets the $SUNCC variable to "yes" or "no"
dnl **********************************************************************
AC_DEFUN([DETECT_SUNCC],
[
    AC_CHECK_DECL([__SUNPRO_C], [SUNCC="yes"], [SUNCC="no"])
    AS_IF(test "x$SUNCC" = "xyes", [$1], [$2])

])

DETECT_SUNCC([CFLAGS="-mt $CFLAGS"], [])

if test "$ICC" = "no"; then
   AC_PROG_CC_C99
fi

AM_PROG_CC_C_O
AC_PROG_INSTALL

AC_ARG_ENABLE(64bit,
  [AS_HELP_STRING([--enable-64bit],[build 64bit version])])
if test "x$enable_64bit" = "xyes"
then
    org_cflags=$CFLAGS
    CFLAGS=-m64
    AC_RUN_IFELSE(
      [AC_LANG_PROGRAM([], [dnl
return sizeof(void*) == 8 ? 0 : 1;
      ])
    ],[
      CFLAGS="-m64 $org_cflags"
    ],[
    AC_MSG_ERROR([Don't know how to build a 64-bit object.])
    ])
fi

trylibeventdir=""
AC_ARG_WITH(libevent,
       [  --with-libevent=PATH     Specify path to libevent installation ],
       [
                if test "x$withval" != "xno" ; then
                        trylibeventdir=$withval
                fi
       ]
)

dnl ------------------------------------------------------
dnl libevent detection.  swiped from Tor.  modified a bit.

LIBEVENT_URL=http://www.monkey.org/~provos/libevent/

AC_CACHE_CHECK([for libevent directory], ac_cv_libevent_dir, [
  saved_LIBS="$LIBS"
  saved_LDFLAGS="$LDFLAGS"
  saved_CPPFLAGS="$CPPFLAGS"
  le_found=no
  for ledir in $trylibeventdir "" $prefix /usr/local ; do
    LDFLAGS="$saved_LDFLAGS"
    LIBS="$saved_LIBS -levent"

    # Skip the directory if it isn't there.
    if test ! -z "$ledir" -a ! -d "$ledir" ; then
       continue;
    fi
    if test ! -z "$ledir" ; then
      if test -d "$ledir/lib" ; then
        LDFLAGS="-L$ledir/lib $LDFLAGS"
      else
        LDFLAGS="-L$ledir $LDFLAGS"
      fi
      if test -d "$ledir/include" ; then
        CPPFLAGS="-I$ledir/include $CPPFLAGS"
      else
        CPPFLAGS="-I$ledir $CPPFLAGS"
      fi
    fi
    # Can I compile and link it?
    AC_TRY_LINK([#include <sys/time.h>
#include <sys/types.h>
#include <event.h>], [ event_init(); ],
       [ libevent_linked=yes ], [ libevent_linked=no ])
    if test $libevent_linked = yes; then
       if test ! -z "$ledir" ; then
         ac_cv_libevent_dir=$ledir
         _myos=`echo $target_os | cut -f 1 -d .`
         AS_IF(test "$SUNCC" = "yes" -o "x$_myos" = "xsolaris2",
               [saved_LDFLAGS="$saved_LDFLAGS -Wl,-R$ledir/lib"],
               [AS_IF(test "$GCC" = "yes",
                     [saved_LDFLAGS="$saved_LDFLAGS -Wl,-rpath=$ledir/lib"])])
       else
         ac_cv_libevent_dir="(system)"
       fi
       le_found=yes
       break
    fi
  done
  LIBS="$saved_LIBS"
  LDFLAGS="$saved_LDFLAGS"
  CPPFLAGS="$saved_CPPFLAGS"
  if test $le_found = no ; then
    AC_MSG_ERROR([libevent is required.  You can get it from $LIBEVENT_URL

      If it's already installed, specify its path using --with-libevent=/dir/
])
  fi
])
LIBS="$LIBS -levent"
if test $ac_cv_libevent_dir != "(system)"; then
  if test -d "$ac_cv_libevent_dir/lib" ; then
    LDFLAGS="-L$ac_cv_libevent_dir/lib $LDFLAGS"
    le_libdir="$ac_cv_libevent_dir/lib"
  else
    LDFLAGS="-L$ac_cv_libevent_dir $LDFLAGS"
    le_libdir="$ac_cv_libevent_dir"
  fi
  if test -d "$ac_cv_libevent_dir/include" ; then
    CPPFLAGS="-I$ac_cv_libevent_dir/include $CPPFLAGS"
  else
    CPPFLAGS="-I$ac_cv_libevent_dir $CPPFLAGS"
  fi
fi

dnl ----------------------------------------------------------------------------


# Check for pkg-config program, used for configuring some libraries.
#
m4_define_default([PKG_PROG_PKG_CONFIG],
  [AC_MSG_CHECKING([pkg-config])
   AC_MSG_RESULT([no])])
PKG_PROG_PKG_CONFIG

# If the pkg-config autoconf support isn't installed, define its
# autoconf macro to disable any packages depending on it.
#
m4_define_default([PKG_CHECK_MODULES],
  [AC_MSG_CHECKING([$1])
   AC_MSG_RESULT([no])
   $4])

# Tries to find the header file $2 (in C <> or "" syntax) with the
# various include directories specified in $3 (space-separated) added to
# the include path.
#
# If found, then the absolute include directory used is assigned to $1
# and the shell statement in $4 is evaluted; if not found anywhere, $1
# is set to "", and the shell statement in $5 are evaluated.
#
AC_DEFUN([SNOGRAY_CHECK_SUBDIR_HEADER], [
    _SAVED_CPPFLAGS="$CPPFLAGS"
    AC_MSG_CHECKING([for $2 include dir])
    _snogray_check_subdir_header_dir=""
    for _snogray_check_subdir_header_root in "$includedir" "/usr/local/include" "/usr/include"
    do
      for _snogray_check_subdir_header_sfx in $3; do
	_snogray_check_subdir_header_dir="$_snogray_check_subdir_header_root/$_snogray_check_subdir_header_sfx"
	CPPFLAGS="$_SAVED_CPPFLAGS -I$_snogray_check_subdir_header_dir"
	AC_COMPILE_IFELSE([AC_LANG_SOURCE([[#include $2]])], [break 2])
	_snogray_check_subdir_header_dir=""
      done
    done
    CPPFLAGS="$_SAVED_CPPFLAGS"
    _snogray_check_subdir_header_result=
    if test x"$_snogray_check_subdir_header_dir" != x""; then
      _snogray_check_subdir_header_result="$_snogray_check_subdir_header_dir"
      $1="$_snogray_check_subdir_header_dir"
      $4
    else
      _snogray_check_subdir_header_result=none
      $1=""
      $5
    fi
    AC_MSG_RESULT([$_snogray_check_subdir_header_result])
])


PKG_CHECK_MODULES([liblua], [lua5.1], [have_liblua=yes], [:])

if test $have_liblua = no; then
  AC_LANG_PUSH(C)
  have_lua_h=no
  lua_include_flags=""
  AC_CHECK_HEADER([lua.h], [have_lua_h=yes])
  if test $have_lua_h = no; then
    SNOGRAY_CHECK_SUBDIR_HEADER(
       [lua_include_dir], [<lua.h>],
       [lua5.1 lua-5.1 lua51 lua],
       [have_lua_h=yes; lua_include_flags="-I$lua_include_dir"])
  fi
  if test $have_lua_h = yes; then
    for lua_lib_name in lua lua5.1 lua-5.1 lua51; do
      AC_CHECK_LIB([$lua_lib_name], [lua_close], [have_liblua=yes], [have_liblua=no], [-lm])
      if test $have_liblua = yes; then
	AC_SUBST([liblua_CFLAGS], ["$lua_include_flags"])
	AC_SUBST([liblua_LIBS], ["-l$lua_lib_name"])
	break
      fi
    done
  fi
  AC_LANG_POP(C)
fi
if test $have_liblua = yes; then
  AC_DEFINE([HAVE_LIBLUA], [1], [Define if liblua is installed])

  # Now see if there's a luajit library.  If there is, link against it
  # instead of the standard Lua library; the header files, are still
  # those from the normal Lua installation, and luajit is basically a
  # drop-in replacement for standard Lua, so we dont need to do much
  # else.
  #
  have_libluajit=no
  luajit_lib_name="luajit-5.1"
#  disabling this for now  
#  AC_CHECK_LIB([$luajit_lib_name], [lua_close], [have_libluajit=yes], [:], [-lm])
  if test $have_libluajit = yes; then
    AC_SUBST([liblua_LIBS], ["-l$luajit_lib_name"])
  fi
  LIBS="$liblua_LIBS $LIBS"
  CPPFLAGS="$liblua_CFLAGS $CPPFLAGS"
fi
AM_CONDITIONAL([have_liblua], [test $have_liblua = yes])

AC_SEARCH_LIBS(socket, socket)
AC_SEARCH_LIBS(gethostbyname, nsl)
AC_SEARCH_LIBS(dlopen, dl)
AC_SEARCH_LIBS(floor, m)
AC_CHECK_FUNCS([clock_gettime], [rt])

AC_HEADER_STDBOOL
AH_BOTTOM([#if HAVE_STDBOOL_H
#include <stdbool.h>
#else
#define bool char
#define false 0
#define true 1
#endif ])

AC_CHECK_HEADERS([inttypes.h])
AH_BOTTOM([#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
])

dnl **********************************************************************
dnl DETECT_UINT64_SUPPORT
dnl
dnl check if we can use a uint64_t
dnl **********************************************************************
AC_DEFUN([AC_C_DETECT_UINT64_SUPPORT],
[
    AC_CACHE_CHECK([for print macros for integers (C99 section 7.8.1)],
        [ac_cv_c_uint64_support],
        [AC_TRY_COMPILE(
            [
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#include <stdio.h>
            ], [
  uint64_t val = 0;
  fprintf(stderr, "%" PRIu64 "\n", val);
            ],
            [ ac_cv_c_uint64_support=yes ],
            [ ac_cv_c_uint64_support=no ])
        ])
])

AC_C_DETECT_UINT64_SUPPORT
AS_IF([test "x$ac_cv_c_uint64_support" = "xno"],
      [AC_MSG_WARN([

Failed to use print macros (PRIu) as defined in C99 section 7.8.1.

])])

AC_C_CONST

dnl From licq: Copyright (c) 2000 Dirk Mueller
dnl Check if the type socklen_t is defined anywhere
AC_DEFUN([AC_C_SOCKLEN_T],
[AC_CACHE_CHECK(for socklen_t, ac_cv_c_socklen_t,
[
  AC_TRY_COMPILE([
    #include <sys/types.h>
    #include <sys/socket.h>
  ],[
    socklen_t foo;
  ],[
    ac_cv_c_socklen_t=yes
  ],[
    ac_cv_c_socklen_t=no
  ])
])
if test $ac_cv_c_socklen_t = no; then
  AC_DEFINE(socklen_t, int, [define to int if socklen_t not available])
fi
])

AC_C_SOCKLEN_T

dnl Check if we're a little-endian or a big-endian system, needed by hash code
AC_DEFUN([AC_C_ENDIAN],
[AC_CACHE_CHECK(for endianness, ac_cv_c_endian,
[
  AC_RUN_IFELSE(
    [AC_LANG_PROGRAM([], [dnl
        long val = 1;
        char *c = (char *) &val;
        exit(*c == 1);
    ])
  ],[
    ac_cv_c_endian=big
  ],[
    ac_cv_c_endian=little
  ])
])
if test $ac_cv_c_endian = big; then
  AC_DEFINE(ENDIAN_BIG, 1, [machine is bigendian])
fi
if test $ac_cv_c_endian = little; then
  AC_DEFINE(ENDIAN_LITTLE, 1, [machine is littleendian])
fi
])

AC_C_ENDIAN

AC_DEFUN([AC_C_HTONLL],
[
    AC_MSG_CHECKING([for htonll])
    have_htoll="no"
    AC_TRY_LINK([
#include <sys/types.h>
#include <netinet/in.h>
#ifdef HAVE_INTTYPES_H
#include <inttypes.h> */
#endif
       ], [
          return htonll(0);
       ], [
          have_htoll="yes"
          AC_DEFINE([HAVE_HTONLL], [1], [Have ntohll])
    ], [
          have_htoll="no"
    ])

    AC_MSG_RESULT([$have_htoll])
])

AC_C_HTONLL

AC_DEFUN([AC_C_ALIGNMENT],
[AC_CACHE_CHECK(for alignment, ac_cv_c_alignment,
[
  AC_RUN_IFELSE(
    [AC_LANG_PROGRAM([
#include <stdlib.h>
#include <inttypes.h>
    ], [
       char *buf = malloc(32);

       uint64_t *ptr = (uint64_t*)(buf+2);
       // catch sigbus, etc.
       *ptr = 0x1;

       // catch unaligned word access (ARM cpus)
       *buf =  1; *(buf +1) = 2; *(buf + 2) = 2; *(buf + 3) = 3; *(buf + 4) = 4;
       int* i = (int*)(buf+1);
       return (84148994 == i) ? 0 : 1;
    ])
  ],[
    ac_cv_c_alignment=none
  ],[
    ac_cv_c_alignment=need
  ],[
    ac_cv_c_alignment=need
  ])
])
if test $ac_cv_c_alignment = need; then
  AC_DEFINE(NEED_ALIGN, 1, [Machine need alignment])
fi
])

AC_C_ALIGNMENT

dnl Let the compiler be a bit more picky. Please note that you cannot
dnl specify these flags to the compiler before AC_CHECK_FUNCS, because
dnl the test program will generate a compilation warning and hence fail
dnl to detect the function ;-)
if test "$ICC" = "yes"
then
   dnl ICC trying to be gcc.
   CFLAGS="$CFLAGS -diag-disable 187 -Wall -Werror"
   AC_DEFINE([_GNU_SOURCE],[1],[find sigignore on Linux])
elif test "$GCC" = "yes"
then
  GCC_VERSION=`$CC -dumpversion`
  CFLAGS="$CFLAGS -Wall -Werror -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls"
  case $GCC_VERSION in
    4.4.*)
    CFLAGS="$CFLAGS -fno-strict-aliasing"
    ;;
  esac
  AC_DEFINE([_GNU_SOURCE],[1],[find sigignore on Linux])
elif test "$SUNCC" = "yes"
then
  CFLAGS="$CFLAGS -errfmt=error -errwarn -errshort=tags"
fi

AC_CONFIG_FILES([Makefile 
                 src/Makefile 
                 src/io/Makefile
                 src/lua/Makefile
                 src/hashmap/Makefile
                 src/parser/Makefile
                 src/datastream/Makefile
                 src/chunkpool/Makefile
                 src/common/Makefile
                 src/fallocator/Makefile
                 src/cacheitem/Makefile
                 src/cluster/Makefile])
AC_OUTPUT
