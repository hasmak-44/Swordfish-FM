# Swordfish FM portable changelog

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
