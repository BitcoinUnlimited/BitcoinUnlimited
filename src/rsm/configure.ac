AC_PREREQ(2.60)
define(_CLIENT_VERSION_MAJOR, 1)
define(_CLIENT_VERSION_MINOR, 0)
define(_CLIENT_VERSION_REVISION, 1)
AC_INIT([Recursive Shared Mutex],m4_join([.],_CLIENT_VERSION_MAJOR,_CLIENT_VERSION_MINOR,_CLIENT_VERSION_REVISION),[https://github.com/Greg-Griffith/cxx_recursive_shared_mutex/issues],[RSM])
AC_CONFIG_SRCDIR([lib/recursive_shared_mutex.cpp])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([build-aux/m4])
AC_CONFIG_HEADERS([rsm-config.h])

AC_CANONICAL_HOST

AH_TOP([#ifndef RSM_CONFIG_H])
AH_TOP([#define RSM_CONFIG_H])
AH_BOTTOM([#endif //RSM_CONFIG_H])

AM_INIT_AUTOMAKE([no-define subdir-objects foreign])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CXX
AC_LANG([C++])

dnl By default, libtool for mingw refuses to link static libs into a dll for
dnl fear of mixing pic/non-pic objects, and import/export complications. Since
dnl we have those under control, re-enable that functionality.
case $host in
  *mingw*)
     lt_cv_deplibs_check_method="pass_all"
  ;;
esac

AX_CXX_COMPILE_STDCXX([14], [noext], [mandatory], [nodefault])

LT_INIT

AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug], [use debug compiler flags and macros (default is no)])],
    [enable_debug=$enableval],
    [enable_debug=no])

AC_ARG_ENABLE(tests,
    AS_HELP_STRING([--disable-tests],[do not compile tests (default is to compile)]),
    [use_tests=$enableval],
    [use_tests=yes])

AC_ARG_ENABLE(experimental,
    AS_HELP_STRING([--enable-experimental],[compile experimental branch and tests (default is not to compile)]),
    [enable_experimental=$enableval],
    [enable_experimental=no])

if test "x$enable_debug" = xyes; then
    CPPFLAGS="$CPPFLAGS -DRSM_DEBUG_ASSERTION"
fi

case $host in
  *mingw*)
    LIBTOOL_APP_LDFLAGS="$LIBTOOL_APP_LDFLAGS -all-static"
  ;;
esac

dnl Check for pthread compile/link requirements
AX_PTHREAD

if test "x$use_tests" = xyes; then

    dnl Minimum required Boost version
    define(MINIMUM_REQUIRED_BOOST, 1.55.0)

    dnl Check for boost libs
    AX_BOOST_BASE([MINIMUM_REQUIRED_BOOST])
    AX_BOOST_SYSTEM
    AX_BOOST_UNIT_TEST_FRAMEWORK
    if test x$BOOST_SYSTEM_LIB = x; then
        AC_MSG_ERROR([Boost system library required.])
    fi

    dnl Determine if -DBOOST_TEST_DYN_LINK is needed
    AC_MSG_CHECKING([for dynamic linked boost test])
    TEMP_LIBS="$LIBS"
    LIBS="$LIBS $BOOST_LDFLAGS $BOOST_UNIT_TEST_FRAMEWORK_LIB"
    TEMP_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $BOOST_CPPFLAGS"
    AC_LINK_IFELSE([AC_LANG_SOURCE([
        #define BOOST_TEST_DYN_LINK
        #define BOOST_TEST_MAIN
        #include <boost/test/unit_test.hpp>
    ])],
    [AC_MSG_RESULT(yes)]
    [TESTDEFS="$TESTDEFS -DBOOST_TEST_DYN_LINK"],
        [AC_MSG_RESULT(no)])
    LIBS="$TEMP_LIBS"
    CPPFLAGS="$TEMP_CPPFLAGS"

    BOOST_LIBS="$BOOST_LDFLAGS $BOOST_SYSTEM_LIB $BOOST_UNIT_TEST_FRAMEWORK_LIB"
fi

AC_MSG_CHECKING([whether to build test_rsm])
if test x$use_tests = xyes; then
  AC_MSG_RESULT([yes])
  BUILD_TEST="yes"
else
  AC_MSG_RESULT([no])
  BUILD_TEST=""
fi

AC_MSG_CHECKING([whether to build experimental code])
if test x$enable_experimental = xyes; then
  AC_MSG_RESULT([yes])
  BUILD_EXPERIMENTAL="yes"
else
  AC_MSG_RESULT([no])
  BUILD_EXPERIMENTAL=""
fi

AC_CONFIG_FILES([Makefile])

AM_CONDITIONAL([ENABLE_TESTS],[test x$BUILD_TEST = xyes])
AM_CONDITIONAL([ENABLE_EXPERIMENTAL],[test x$BUILD_EXPERIMENTAL = xyes])

AC_SUBST(LIBTOOL_APP_LDFLAGS)
AC_SUBST(RELDFLAGS)
AC_SUBST(BUILD_EXEEXT)
AC_SUBST(BOOST_LIBS)
AC_SUBST(TESTDEFS)
AC_OUTPUT
