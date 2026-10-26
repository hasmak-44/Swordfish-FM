AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = swf-automount

swf_automount_SOURCES = xfe-automount.cpp

swf_automount_LDADD = $(UDISKS2_LIBS) $(LIBNOTIFY_LIBS)

swf_automount_CXXFLAGS = -std=c++17 $(UDISKS2_CFLAGS) $(LIBNOTIFY_CFLAGS)
