AUTOMAKE_OPTIONS = foreign no-dependencies

EXTRA_DIST = libiptc.c README.collectd

if COMPILER_IS_GCC
AM_CFLAGS = -Wall -Werror
endif

noinst_LTLIBRARIES = libiptc.la

libiptc_la_CFLAGS = -I$(KERNEL_DIR)/include
libiptc_la_SOURCES = libip4tc.c libip6tc.c \
		ipt_kernel_headers.h libip6tc.h libiptc.h linux_list.h \
		xtables.h libxtc.h

