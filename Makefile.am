AM_CFLAGS = -Ilibdivsufsort/include

bin_PROGRAMS = bsdiff bspatch

bsdiff_SOURCES = bsdiff.c

bspatch_SOURCES = bspatch.c

bsdiff_CFLAGS = -DBSDIFF_EXECUTABLE -Ilibdivsufsort/include
bspatch_CFLAGS = -DBSPATCH_EXECUTABLE

bsdiff_LDFLAGS = -lbz2 -Llibdivsufsort/lib -ldivsufsort
bspatch_LDFLAGS = -lbz2

EXTRA_DIST = bsdiff.h bspatch.h

include_HEADERS=bsdiff.h bspatch.h
lib_LTLIBRARIES=libbsdiff.la
libbsdiff_la_SOURCES=bsdiff.c bspatch.c

SUBDIRS = libdivsufsort


