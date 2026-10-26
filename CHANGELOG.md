# Swordfish FM portable changelog

## Unreleased

### Changed
- Swordfish no longer reads Xfe's system settings (`/etc/xfe`,
  `/usr/share/xfe`, `/usr/local/share/xfe`, `xferc`). An installed Swordfish
  uses only its own `share/swordfish` folder and `~/.config/swordfish`, so it
  cannot pick up or disturb an installed Xfe. The portable bundle is unchanged.
- Removed the inherited Xfe Debian packaging (`debian/`), which still built a
  package named `xfe`, and Xfe's `.run` installer scripts (`buildrun`,
  `installrun`, `uninstall-xfe`), which install or delete `/usr/local/bin/xfe`
  and `/usr/local/share/xfe`.
- Credits to Xfe, the FOX Toolkit and X Win Commander are kept in AUTHORS.

## swordfish-0.0.1 — first stable release

Changes since `swordfish-portable-0.0.1-rc5`. Full details are in
[PROJECT-HISTORY.md](PROJECT-HISTORY.md), section 3.16.

### Added
- Visible device-row buttons: Eject (safely remove) for USB drives, Unmount
  for mounted internal partitions and network shares.
- Unmounted internal partitions and drives stay listed greyed out; click to
  mount (asks for the admin password where the system requires it).
- Connect to Server dialog with SMB/SFTP/FTP/FTPS/DAV/DAVS, port and optional
  share; credentials are requested only if the server needs them.
- Inline rename, drag selection with auto-scroll, path editor popup, and
  one-second tab hover switching while dragging.

### Fixed
- Clicking a discovered server browses it in the active tab instead of
  opening new tabs.
- Unmount closes every tab on the device in both panes first, cancels
  thumbnail previews and folder-size scans reading it, and retries once if
  the device is briefly busy. USB Eject/Unmount no longer report "busy".
- Mouse input is released to the desktop before mount, unmount or navigation
  starts, so a slow device cannot freeze the taskbar and dock.
- Root, Home and the filesystem containing Home cannot be unmounted;
  Mount/Unmount need an explicitly selected device.
- Unmounted volume inventory (`lsblk` argument conflict) and filesystem-type
  parsing, so unmounted devices no longer disappear.
- Existing network mounts are reused instead of mounted twice.
- Tab bars keep their geometry through hide/show and orientation changes.
- About shows the real version; About and Help link to the repository.

### Removed
- Thumbnail-size and common-address-bar settings.


## swordfish-portable-0.0.1-testing7

Changes since `swordfish-portable-0.0.1-testing6`.

### Added
- **Open Archive as Folder / Close Archive Folder** in the file context menu.
  Archives (tar and compressed tar, zip, 7z, rar, iso, cpio, lzh) are mounted
  read/write with `archivemount` (the host's copy; the entry is hidden if it is
  not installed). The menu adapts: an unopened archive offers Open, an open one
  or its mounted folder offers Close. Close moves panels out of the folder,
  saves the archive with a "Saving archive" dialog, and removes the empty
  `_archive` folder. Errors from `archivemount` / `fusermount` are shown.
- Mounted archive folders get the archive icon and the type "Archive Folder".

### Fixed
- Scripts, mounts and unmounts now trigger a forced rescan for a few seconds,
  so newly created or mounted folders appear without pressing Refresh.
- The new item created by a script or mount is selected, focused and scrolled
  into view, and stays selected across refreshes.
- Folders on FUSE mounts (archivemount, sshfs...) are rescanned every 3 seconds,
  because they do not update the folder date. Files added or moved into an
  opened archive folder now show up promptly instead of after ~30 seconds.

### Changed
- `File::mount` can return the command output and status to its caller.

## swordfish-portable-0.0.1-testing6

Changes since `swordfish-portable-0.0.1-testing5`.

### Added
- **System icon themes**: Settings → Appearance lists installed freedesktop icon
  themes as "Name (system)". Choosing one converts it once (SVG rasterized with a
  bundled `rsvg-convert`, resized to the Swordfish icon sizes) and caches it in
  the icons folder (`portable-data/icons/<id>-system`). Only theme names are
  scanned when Preferences opens. Missing icons fall back to the default theme.
  New files: `src/SystemIcons.{h,cpp}`, `icons/system-icon-map.txt`.
- Conversion progress dialog with message, progress bar and time remaining.
- **Language selector** in Settings → Appearance: "System default", English and
  every language found in the bundle's locale folder. The choice is stored in
  `config/swordfish/language`; a restart applies it.
- **Color theme export / import**: custom themes can be saved to and loaded from
  `*.swordfish-theme` text files via buttons or the theme context menu.
- Categorized application logs (application, ui, file-operations, settings,
  errors) written to `portable-data/logs` (`src/AppLogger.{h,cpp}`).

### Changed
- User-visible "Xfe / X File …" names replaced with Swordfish names (Settings,
  key bindings, Swordfish Image / Write / Archive / Package, automounter).
  About/Help attributions and copyright are unchanged.
- Portable build bundles `rsvg-convert` (`librsvg2-bin` added to the Dockerfile).
- Portable launcher, icon loading and file panel/list updates from the earlier
  portable tuning work.
- `portable/README.md` documents system icons and theme sharing.

### Fixed
- Choosing "System default" language after another language now restores the
  system locale instead of keeping the previous language after restart.
