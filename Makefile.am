man_MANS=swordfish.1 swordfish-archive.1 swordfish-image.1 swordfish-package.1 swordfish-write.1

if XFEAUTOMOUNTER
SUBDIRS = po m4 src automount icons
else
SUBDIRS = po m4 src icons
endif

DIST_SUBDIRS = po m4 src automount icons

rcdir = $(prefix)/share/swordfish
rc_DATA = swordfishrc
manualdir = $(rcdir)/manual
dist_manual_DATA = docs/manual/index.html
manualimagesdir = $(manualdir)/images
dist_manualimages_DATA = docs/manual/images/workspace.png docs/manual/images/annotated-workspace.png \
                        docs/manual/images/welcome.png docs/manual/images/preferences.png \
                        docs/manual/images/context-menu.png docs/manual/images/mouse-and-keyboard.svg \
                        docs/manual/images/copy-vs-move.svg
appicondir = $(rcdir)/app-icon
dist_appicon_DATA = app-icon/swordfish-folder-256.png app-icon/swordfish-folder-512.png app-icon/swordfish-folder-512.jpg \
                    app-icon/swordfish-circle-256.png app-icon/swordfish-circle-512.png app-icon/swordfish-circle-512.jpg \
                    app-icon/swordfish-shield-256.jpg app-icon/swordfish-shield-512.jpg
pkdir = $(prefix)/share/polkit-1/actions

EXTRA_DIST = autogen.sh install-swordfish-system.sh iconlinks.sh config.h i18n.h org.swordfish.root.policy.in.in swordfish.1 swordfish-archive.1 swordfish-image.1 swordfish-package.1 swordfish-write.1 \
             portable/AppRun portable/Dockerfile portable/build-portable-in-container portable/build-portable.sh portable/README.md \
             tests/preview-regression.cpp tests/run-preview-regression.sh \
             tests/network-discovery-regression.cpp tests/run-network-discovery-regression.sh \
             tests/folder-expansion-regression.cpp tests/run-folder-expansion-regression.sh \
             tests/search-window-regression.cpp tests/run-search-window-regression.sh \
             tests/system-integration-regression.cpp tests/run-system-integration-regression.sh \
             tests/custom-actions-regression.cpp tests/run-custom-actions-regression.sh \
             tests/custom-actions-ui-regression.cpp tests/run-custom-actions-ui-regression.sh \
             tests/run-command-link-regression.sh \
             tests/fake-remote-directory.py tests/native-directory-helper-regression.py \
             tests/portable-launcher-regression.py \
             ABOUT-NLS TODO BUGS swordfishrc.in *.desktop.in.in \
                 $(top_srcdir)/icons/*

desktopdir = $(prefix)/share/applications
desktop_in_files = swordfish.desktop.in swfa.desktop.in swfi.desktop.in swfp.desktop.in swfw.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
bin_SCRIPTS = swordfish-open

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
