AC_PREREQ(2.59) #autoconf检查版本
AC_INIT([grate], [0.0.0], [thierry.reding@avionic-design.de]) #定义了程序的名称、版本、和错误报告地址，一般情况下BUG-REPORT-ADDRESS可以省略，用户可以根据自己的要求进行定制  
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([Makefile.am]) #检查源文件来确定所在目录包含这个文件
AC_CONFIG_HEADER([config.h]) #会生成一个头文件，此头文件中包含了AC_INIT 和AC_SUBST所定义的宏
AC_CANONICAL_HOST

AM_INIT_AUTOMAKE([no-dist-gzip dist-xz foreign]) #初始化automake，传到这个宏里的参数是要编译的应用程序的名称和版本号(这些参数成为config.h中定义的PACKAGE和VERSION值)
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])]) #如果AM_SILENT_RULES宏存在，那么调用AM_SILENT_RULES([yes]，此宏表示编译的时候只输出重要的log

AC_PROG_LIBTOOL
AC_PROG_CC #设置默认C编译器
AM_PROG_CC_C_O
AC_PROG_CXX
AC_PROG_INSTALL #根据install-sh脚本安装
AM_PROG_LEX
AC_PROG_YACC
PKG_PROG_PKG_CONFIG

PKG_CHECK_MODULES(DevIL, ILU) #检测系统中pkgconfig是否存在ILU.pc文件，DevIL是DevIL_CLFAGS和DevIL_LIBS的前缀，DevIL_CLFAGS对应ILU.pc中的Cflags变量，DevIL_LIBS应ILU.pc中的Libs变量，DevIL_CLFAGS和DevIL_LIBS可以在Makefile.am中使用
PKG_CHECK_MODULES(PNG, libpng) #检测系统中pkgconfig是否存在libpng.pc文件
PKG_CHECK_MODULES(DRM, libdrm) #检测系统中pkgconfig是否存在libdrm.pc文件

AC_CHECK_LIB([z], [adler32], [enable_zlib=yes], [enable_zlib=no])
AM_CONDITIONAL([ENABLE_ZLIB], [test "x$enable_zlib" = "xyes"])

AC_CHECK_LIB([lz4], [LZ4_compress_default], [enable_lz4=yes], [enable_lz4=no])
AM_CONDITIONAL([ENABLE_LZ4], [test "x$enable_lz4" = "xyes"])

AC_ARG_ENABLE([gles-tests],
	[AS_HELP_STRING([--enable-gles-tests],
		[Build GLES tests (default: enabled)])],
		[enable_gles="$enableval"],
		[enable_gles=yes])
AS_IF([test "x$enable_gles" = "xyes"], [
	PKG_CHECK_MODULES(GLES1, [egl glesv1_cm x11], [HAVE_GLES1=1], [HAVE_GLES1=0])
	AM_CONDITIONAL([USE_GLES1], [test "$HAVE_GLES1" -eq 1])
	PKG_CHECK_MODULES(GLES2, [egl glesv2 x11], [HAVE_GLES2=1], [HAVE_GLES2=0])
	AM_CONDITIONAL([USE_GLES2], [test "$HAVE_GLES2" -eq 1])
])

AC_ARG_ENABLE([x11-display],
	[AS_HELP_STRING([--enable-x11-display],
		[Build with X11 display support (default: enabled)])],
		[enable_x11=no],
		[enable_x11=no])
AS_IF([test "x$enable_x11" = "xyes"], [
	PKG_CHECK_MODULES(XCB, [xcb xcb-image xcb-dri2 xcb-icccm], [AC_DEFINE([HAVE_XCB], [1], [Use XCB])])
])

CFLAGS="$CFLAGS -Wall"

AC_ARG_ENABLE([werror],
	[AS_HELP_STRING([--enable-werror],
		[Treat warnings as errors (default: disabled)])],
		[enable_werror="$enableval"],
		[enable_werror=no])
if test "x$enable_werror" = "xyes"; then
	CFLAGS="$CFLAGS -Werror"
fi

AC_CHECK_LIB([cgdrv], [CgDrv_Create], [enable_cgc=yes], [enable_cgc=no])
AM_CONDITIONAL([ENABLE_CGC], [test "x$enable_cgc" = "xyes"])

AC_CHECK_HEADER([envytools/rnn.h],
	[AC_CHECK_LIB([rnn], [rnn_init], [enable_rnn=yes], [enable_rnn=no], [-lenvyutil -lxml2])])
AM_CONDITIONAL([ENABLE_RNN], [test "x$enable_rnn" = "xyes"])

AC_CONFIG_FILES([
	src/libgrate/libgrate.pc
])

AC_OUTPUT([
	Makefile
	include/Makefile
	src/Makefile
	src/libcgc/Makefile
	src/libgrate/Makefile
	src/libhost1x/Makefile
	src/libwrap/Makefile
	tests/Makefile
	tests/drm/Makefile
	tests/gles1/Makefile
	tests/gles2/Makefile
	tests/grate/Makefile
	tests/host1x/Makefile
	tests/nvhost/Makefile
	tools/Makefile
])
