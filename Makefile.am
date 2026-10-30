## Process this file with automake to produce Makefile.in

bin_PROGRAMS = fbv

fbv_SOURCES = bmp.c fb_display.c gif.c jpeg.c main.c png.c transforms.c vt.c

EXTRA_DIST = \
	COPYING README.md VERSION \
	fbv.1 \
	autogen.sh

noinst_HEADERS = \
	fbv.h

man_MANS = fbv.1

clean-local:
	rm -f *~
