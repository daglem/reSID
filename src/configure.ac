dnl This file is part of reSID, a MOS6581 SID emulator engine.
dnl Copyright (C) 1998 - 2022  Dag Lem <resid@nimrod.no>
dnl
dnl Process this file with autoconf to produce a configure script.

dnl Use Autoconf
AC_PREREQ([2.69])
AC_INIT([reSID], [1.0-pre1], [resid@nimrod.no])
AC_CONFIG_SRCDIR([sid.h])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])

dnl Use Automake
AM_INIT_AUTOMAKE([1.16.2])

dnl Use Libtool
LT_PREREQ([2.4.6])
LT_INIT([disable-shared])
AC_SUBST([LTVERSION], [6:0:0])

dnl Use C++ for tests.
AC_LANG([C++])

dnl Enable inlining.
AC_ARG_ENABLE([inline],
  [AS_HELP_STRING([--enable-inline],
    [enable inlining of functions [default=yes]])])

AS_IF([test "$enable_inline" != no],
  [AC_SUBST([RESID_INLINING], [1])
   AC_SUBST([RESID_INLINE], [inline])],
  [AC_SUBST([RESID_INLINING], [0])
   AC_SUBST([RESID_INLINE], [])])

dnl Enable architecture specific compilation.
AC_ARG_ENABLE([arch],
  [AS_HELP_STRING([--enable-arch[[=arch]]],
    [enable architecture specific compilation [default=native]])],
  [],
  [enable_arch=yes])

AS_IF([test "$enable_arch" = no], [march=""],
      [test "$enable_arch" = yes], [march=" -march=native"],
      [march=" -march=$enable_arch"])

dnl Enable branch prediction hints.
AC_ARG_ENABLE([branch-hints],
  [AS_HELP_STRING([--enable-branch-hints],
    [enable static branch prediction hints [default=yes]])])

AS_IF([test "$enable_branch_hints" != no],
  [AC_SUBST([RESID_BRANCH_HINTS], [1])],
  [AC_SUBST([RESID_BRANCH_HINTS], [0])])

dnl Enable FPGA code.
AC_ARG_ENABLE([fpga-code],
  [AS_HELP_STRING([--enable-fpga-code],
    [enable code which can mapped to FPGA implementations [default=no]])])

AS_IF([test "$enable_fpga_code" != yes],
  [AC_SUBST([RESID_FPGA_CODE], [0])],
  [AC_SUBST([RESID_FPGA_CODE], [1])])

dnl Store CXXFLAGS status.
resid_test_CXXFLAGS=${CXXFLAGS+set}

dnl Checks for programs.
AC_PROG_CXX

dnl Set CXXFLAGS for g++.
AS_IF([test "$GXX" = yes -a "$resid_test_CXXFLAGS" != set],
  [resid_save_CXXFLAGS=$CXXFLAGS
   CXXFLAGS="-g -Wall -O3$march -ffast-math -fno-exceptions"
   AC_MSG_CHECKING([whether g++ accepts $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_SOURCE([[int test;]])],
                     [AC_MSG_RESULT(yes)],
                     [AC_MSG_RESULT(no)
                      CXXFLAGS="-g -Wall -O3 -fno-exceptions"
                      AC_MSG_CHECKING([whether g++ accepts $CXXFLAGS])
                      AC_COMPILE_IFELSE([AC_LANG_SOURCE([[int test;]])],
                                        [AC_MSG_RESULT(yes)],
                                        [AC_MSG_RESULT(no)
                                         CXXFLAGS=$resid_save_CXXFLAGS])])])

AC_PATH_PROG([PERL], [perl])

dnl Checks for libraries.

dnl Checks for header files.
AC_CHECK_HEADER([math.h], [], AC_MSG_ERROR([missing math.h]))

dnl Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_SIZEOF([int], [4])

AS_IF([test $ac_cv_sizeof_int -lt 4],
  AC_MSG_ERROR([only 32 bit or better CPUs are supported]))

AC_CACHE_CHECK([for __builtin_expect], [resid_cv_builtin_expect],
  [AC_COMPILE_IFELSE([AC_LANG_SOURCE([[int main() { __builtin_expect(0, 0); }]])],
    [resid_cv_builtin_expect=yes], [resid_cv_builtin_expect=no])])

AS_IF([test "$resid_cv_builtin_expect" = no],
  [AC_SUBST([HAVE_BUILTIN_EXPECT], [0])],
  [AC_SUBST([HAVE_BUILTIN_EXPECT], [1])])

AC_CACHE_CHECK([for consteval], [resid_cv_cxx_consteval],
  [AC_COMPILE_IFELSE([AC_LANG_SOURCE([[consteval int sqr(int n) { return n*n; }]])],
    [resid_cv_cxx_consteval=yes], [resid_cv_cxx_consteval=no])])

AC_CACHE_CHECK([for constexpr exp() and log2()], [resid_cv_cxx_constexpr_cmath],
  [AC_COMPILE_IFELSE([AC_LANG_SOURCE([[#include <cmath>
                                       constexpr double n = log2(10.0)*exp(-10.0);]])],
    [resid_cv_cxx_constexpr_cmath=yes], [resid_cv_cxx_constexpr_cmath=no])])

AS_IF([test "$resid_cv_cxx_constexpr_cmath" = no],
  [AC_SUBST([RESID_CONSTEXPR], [const])
   AC_SUBST([RESID_CONSTEVAL], [])],
  [AC_SUBST([RESID_CONSTEXPR], ["static constexpr"])
   AS_IF([test "$resid_cv_cxx_consteval" = no],
     [AC_SUBST([RESID_CONSTEVAL], [constexpr])],
     [AC_SUBST([RESID_CONSTEVAL], [consteval])])])

AC_CACHE_CHECK([for constinit], [resid_cv_cxx_constinit],
  [AC_COMPILE_IFELSE([AC_LANG_SOURCE([[constinit int a = 10;]])],
    [resid_cv_cxx_constinit=yes], [resid_cv_cxx_constinit=no])])

AS_IF([test "$resid_cv_cxx_constinit" = no],
  [AC_SUBST([RESID_CONSTINIT], [])],
  [AC_SUBST([RESID_CONSTINIT], [constinit])])

dnl Checks for library functions.

AC_CONFIG_FILES([Makefile siddefs.h])
AC_OUTPUT
