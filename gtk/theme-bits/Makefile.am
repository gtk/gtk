include $(top_srcdir)/Makefile.decl

INCLUDES = \
	-I$(top_srcdir)		\
	 -I$(top_builddir) 	\
	$(GDK_PIXBUF_DEP_CFLAGS)

noinst_PROGRAMS = decompose-bits

decompose_bits_SOURCES = decompose-bits.c
decompose_bits_LDADD = $(top_builddir)/gdk-pixbuf/libgdk_pixbuf-$(GTK_API_VERSION).la $(GLIB_LIBS)

EXTRA_DIST += check-13.png check-13-inconsistent.png radio-13.png radio-13-inconsistent.png

-include $(top_srcdir)/git.mk
