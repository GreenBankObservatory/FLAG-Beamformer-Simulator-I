# Copyright (C) 2015 Associated Universities, Inc. Washington DC, USA.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
# Correspondence concerning GBT software should be addressed as follows:
# GBT Operations
# National Radio Astronomy Observatory
# P. O. Box 2
# Green Bank, WV 24944-0002 USA


#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.63])
AC_INIT([FULL-PACKAGE-NAME], [VERSION], [BUG-REPORT-ADDRESS])
AC_CONFIG_AUX_DIR(config)
AM_INIT_AUTOMAKE([foreign])
LT_INIT
AM_SILENT_RULES([yes])
# AC_CONFIG_SRCDIR([hello_world_databuf.h])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Set CFLAGS to nothing if it is not set by the user.  This prevents AC_PROG_CC
# from setting the (supposedly reserved-for-the-user!) variable CFLAGS in
# Makefile, which prevents AM_CFLAGS in Makefile.am from setting an
# optimization level.  For more details, see
# http://lists.gnu.org/archive/html/autoconf/2006-04/msg00007.html
AS_VAR_SET_IF(CFLAGS,[],[CFLAGS=])

# Checks for programs.
AC_PROG_CC

# Check for HASHPIPE and xGPU
AX_CHECK_HASHPIPE
#AX_CHECK_XGPUINFO

# Checks for libraries.
AC_CHECK_LIB([pthread], [pthread_create])
AC_CHECK_LIB([rt], [clock_gettime])
AC_CHECK_LIB([z], [crc32])

# Checks for header files.
AC_CHECK_HEADERS([netdb.h stdint.h stdlib.h string.h sys/socket.h sys/time.h unistd.h zlib.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([clock_gettime memset socket crc32])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
