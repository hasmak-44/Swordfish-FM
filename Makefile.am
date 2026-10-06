man_MANS=swordfish.1 swordfish-archive.1 swordfish-image.1 swordfish-package.1 swordfish-write.1

if XFEAUTOMOUNTER
SUBDIRS = po m4 src automount icons
else
SUBDIRS = po m4 src icons
endif

DIST_SUBDIRS = po m4 src automount icons

rcdir = $(prefix)/share/swordfish
rc_DATA = swordfishrc
pkdir = $(prefix)/share/polkit-1/actions

EXTRA_DIST = autogen.sh buildrun installrun install-swordfish-system.sh uninstall-swordfish iconlinks.sh config.h i18n.h org.swordfish.root.policy.in.in swordfish.1 swordfish-archive.1 swordfish-image.1 swordfish-package.1 swordfish-write.1 \
             portable/AppRun portable/build-portable.sh portable/README.md \
             ABOUT-NLS TODO BUGS swordfishrc.in *.desktop.in.in \
                 $(top_srcdir)/icons/* $(top_srcdir)/debian

desktopdir = $(prefix)/share/applications
desktop_in_files = swordfish.desktop.in swfa.desktop.in swfi.desktop.in swfp.desktop.in swfw.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

polkit_action_in_in_FILES = org.swordfish.root.policy.in.in
polkit_action_in_FILES = org.swordfish.root.policy.in
polkit_action_FILES = $(polkit_action_in_FILES:.policy.in=.policy)
polkit_actiondir = $(datadir)/polkit-1/actions
if INSTALL_POLKIT_ACTIONS
  polkit_action_DATA = $(polkit_action_FILES)
endif

do_subst = sed -e 's,[@]bindir[@],$(bindir),g'

swordfish.desktop: swordfish.desktop.in Makefile
	$(AM_V_GEN)$(MSGFMT) --desktop -d $(top_srcdir)/po --template $< -o $@
swfa.desktop: swfa.desktop.in Makefile
	$(AM_V_GEN)$(MSGFMT) --desktop -d $(top_srcdir)/po --template $< -o $@
swfi.desktop: swfi.desktop.in Makefile
	$(AM_V_GEN)$(MSGFMT) --desktop -d $(top_srcdir)/po --template $< -o $@
swfp.desktop: swfp.desktop.in Makefile
	$(AM_V_GEN)$(MSGFMT) --desktop -d $(top_srcdir)/po --template $< -o $@
swfw.desktop: swfw.desktop.in Makefile
	$(AM_V_GEN)$(MSGFMT) --desktop -d $(top_srcdir)/po --template $< -o $@

org.swordfish.root.policy: org.swordfish.root.policy.in Makefile
	$(AM_V_GEN)$(MSGFMT) --xml -d $(top_srcdir)/po --template $< -o $@

org.swordfish.root.policy.in: org.swordfish.root.policy.in.in Makefile
	$(do_subst) < org.swordfish.root.policy.in.in > org.swordfish.root.policy.in

dist-hook:
	cd po && $(MAKE) update-po
	cp po/*.po* $(distdir)/po
	rm -rf $(distdir)/po/*~
	rm -rf $(distdir)/debian/swordfish

install-data-hook:
	$(MKDIR_P) $(DESTDIR)$(rcdir)
	$(MKDIR_P) $(DESTDIR)$(rcdir)/icons/default-theme
	$(MKDIR_P) $(DESTDIR)$(rcdir)/icons/gnome-theme
	$(MKDIR_P) $(DESTDIR)$(rcdir)/icons/kde-theme
	$(MKDIR_P) $(DESTDIR)$(rcdir)/icons/xfce-theme
	test -f $(DESTDIR)$(rcdir)/swordfishrc || touch $(DESTDIR)$(rcdir)/swordfishrc
	test -f $(DESTDIR)$(rcdir)/swfbrc || touch $(DESTDIR)$(rcdir)/swfbrc
	test -f $(DESTDIR)$(rcdir)/swfarc || touch $(DESTDIR)$(rcdir)/swfarc
	test -f $(DESTDIR)$(rcdir)/swfirc || touch $(DESTDIR)$(rcdir)/swfirc
	test -f $(DESTDIR)$(rcdir)/swfprc || touch $(DESTDIR)$(rcdir)/swfprc
	test -f $(DESTDIR)$(rcdir)/swfwrc || touch $(DESTDIR)$(rcdir)/swfwrc
	bash $(top_srcdir)/iconlinks.sh $(top_srcdir) $(DESTDIR)$(rcdir)
if INSTALL_POLKIT_ACTIONS
	$(MKDIR_P) $(DESTDIR)$(pkdir)
	cp org.swordfish.root.policy $(DESTDIR)$(pkdir)
endif

uninstall-hook:
	rm -rf $(DESTDIR)$(rcdir)
if INSTALL_POLKIT_ACTIONS
	rm -f $(DESTDIR)$(pkdir)/org.swordfish.root.policy
endif

ACLOCAL_AMFLAGS = -I m4
