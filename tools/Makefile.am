#PROGRAMS表示生成可执行文件，noinst表示可执行文件在安装时不被安装到系统中，assembler就是可执行文件的名称
noinst_PROGRAMS = \
	assembler \
	cgc \
	hex2float \
	fp20 \
	fx10 \
	replay \
	reset3d


#表示生成可执行程序assembler时用到的预处理参数，assembler是noinst_PROGRAMS指定的
assembler_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src/libgrate
#表示链接assembler时所需要的库文件
assembler_LDADD = \
	../src/libgrate/libgrate.la

cgc_CPPFLAGS = \
	-I$(top_srcdir)/include

cgc_LDADD = \
	../src/libcgc/libcgc.la

replay_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src/libhost1x \
	-I$(top_srcdir)/src/libgrate

if ENABLE_ZLIB
replay_CPPFLAGS += -DENABLE_ZLIB
endif

if ENABLE_LZ4
replay_CPPFLAGS += -DENABLE_LZ4
endif

replay_CFLAGS = $(ZLIB_CFLAGS)

replay_LDADD = \
	../src/libgrate/libgrate.la
if ENABLE_ZLIB
replay_LDADD += -lz
endif
if ENABLE_LZ4
replay_LDADD += -llz4
endif
