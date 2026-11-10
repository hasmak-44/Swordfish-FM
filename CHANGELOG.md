# Swordfish FM portable changelog

## swordfish-0.0.3-beta2 — 10 November 2026

Published as **0.0.3 beta 2**, the latest release, after the user accepted
baseline test 7. This is beta software, not a declaration of final 0.0.3
stability. The stable 0.0.2 release remains available unchanged.

### Changed
- Custom Action Properties now has a single **Command** field (program plus
  arguments, as typed in a terminal). The separate Program/Parameters fields,
  the Advanced full-shell-command switch and the command preview were removed.
  Actions saved with Program/Parameters open as one equivalent command and stay
  unchanged unless edited.
- **Show on custom toolbar** for actions moved to the Appearance Conditions tab.
- Connected, unmounted USB drives and internal partitions, and remembered
  disconnected network shares, are dimmed with muted theme-aware text. The
  **(unmounted)** label is shown only if the theme cannot dim them legibly.
  They remain clickable for mounting or reconnection; normal text is retained
  if muting would make it harder to read.
- Mount, Unmount and Eject now use solid play, pause and eject symbols inside a
  square frame, with pixel-sharp edges,
  black on light backgrounds and white on dark ones, in device rows, device
  menus and the Tools menu/toolbar. Close Archive Folder keeps its icon.

### Added
- Network discovery also lists advertised **media servers** (UPnP/DLNA),
  **FTP** and **SFTP** servers, **printers** and **routers / access points**,
  each labelled with its kind. Devices are found only by what they announce
  (SSDP, Zeroconf, WS-Discovery); nothing is port-scanned.
- Media servers are browsed read-only like folders. Double-click a file to
  play it with the audio/video player set in Swordfish, else the desktop's
  default application for that file type, else the browser. Right-click a
  file for **Play**, **Play With** (the default application and installed
  players or viewers that accept a web address, the web browser, or
  **Other Application...** for the regular Open With dialog) and **Copy Stream
  Address**. **Download...** saves copies without
  overwriting existing files and shows a progress window (file, bytes, bar)
  with **Hide** and **Cancel Downloads**; cancelling removes the unfinished
  file. FTP/SFTP entries connect like the address bar;
  printers and routers open their settings page in the browser, and printers
  also offer the local printer settings.
- New built-in **MX Blues** color theme, listed right after Current Theme in
  Preferences > Appearance.
- The Trash view uses Caja's Name, Size, Type, Trashed On and Original
  Location columns, with **Restore Selected Items** and **Empty Trash**
  controls above the list. Restore returns selected items to their original
  locations.
- Preferences > Appearance > **Tool Bar Icon Size** sets the custom toolbar's icon
  size using **16 px**, **24 px** or **32 px** radio buttons, scaled for display
  DPI. Accept applies the change; Cancel discards it. Image and
  system icons are reloaded from their source at the new size, preserving their
  proportions; built-in fallback icons are enlarged. The built-in toolbars and
  menus are unchanged.

### Fixed
- FTP servers mounted from the address bar can now be unmounted; the GVFS
  mount URI lookup now recognizes FTP as well as SFTP.
- Trash now combines Home Trash with the current user's standard Trash folders
  on mounted drives, including items trashed by other file managers. Restore
  uses each item's own original location; Empty Trash and Trash Size cover the
  combined view. Same-named items on different drives remain distinct. Old
  private portable Trash contents are not migrated or deleted automatically.
- Dragging files from Swordfish into another program (browser upload, text
  editor) now offers Copy instead of Move, which most programs require. Drags
  inside Swordfish keep the Copy/Move/Link choice. The UI log records whether
  the other program accepted the drop (`drag-to-other-program`).
- A click with a slight mouse movement in a file list no longer shows a stop
  sign. The drag cursor now updates as soon as the drop target answers, and the
  Move/Copy choice is based on the folder being dragged from.
- A drag no longer ends when Swordfish is hidden mid-drag (switching desktops,
  or a panel/taskbar bringing another program forward), so files can be dropped
  into programs on other desktops or restored from the panel. This also applies
  to drags from the folder tree, which no longer cancel when hovering opens a
  folder.
- Saving unrelated preferences no longer reapplies System Integration or
  attempts to remove a modified launcher. Ownership errors preserve the recorded
  choices and disable only integration controls, allowing other settings to save.
- Reapplying unchanged desktop/menu launchers leaves their permissions and
  timestamps untouched instead of unnecessarily changing trust-related metadata.
  Previously every Preferences Accept rewrote both launchers, so desktops
  redrew them as if they had been recreated.
- Launchers that a desktop environment or older build reformatted are still
  recognised as Swordfish's own (marker comment or recorded Exec line), so the
  integration choices stay checked and changing other settings no longer shows
  "Refusing to remove a changed or unrelated desktop entry".
- Launching a portable copy from another Swordfish copy preserves the original
  host library, GIO module and desktop settings instead of recording the older
  bundle's private environment as the host environment.
- Startup no longer scans every installed icon theme when Custom Actions use
  system (`theme:`) icons.

## swordfish-0.0.3-beta1 — 6 November 2026

Published as **0.0.3 beta 1**, the latest release, following user acceptance of
Test 8. This is beta software, not a declaration of final 0.0.3 stability.
The stable 0.0.2 release remains available unchanged.

### Added
- Custom Actions editor with nested folders, cascading context menus,
  separators, selection/pattern conditions, safe path placeholders,
  confirmation and terminal execution. A separate custom toolbar displays
  checked actions or folder dropdowns; its configuration stays separate from
  the built-in main toolbars.
- Custom-action file patterns accept `.ext` shorthand as well as `*.ext`,
  including compound endings such as `.tar.gz`. Folder actions also work on
  the current folder without a selection; an explicit background option remains.
- Top-level Custom Actions menu and Preferences page, context-menu editing
  access, and a Thunar-style action editor with Basic/Appearance Conditions
  tabs, command browsing, icon preview and prominent no-selection setting.
- Separate custom-action Program/Parameters fields and working directory,
  live preview, scrollable parameter legend, and non-executing Test Action
  checks. Existing full commands remain supported. Added selection-count,
  basename-without-extension and extension placeholders.
- Searchable installed-system-icon chooser for custom actions and groups,
  with a maximizable, categorized icon grid and a default/no-custom-icon option.
  Image Files opens the normal file chooser with a preview below its left sidebar.
  Host theme names remain usable in portable mode; previews stay bounded.
- Duplicate Action, explicit installed-program icon suggestions, and
  unsaved-change protection in the action editor and properties.
- Illustrated offline beginner User Manual, available from Help and the
  first-run welcome; the same HTML/images are suitable for online hosting.
- Optional System Integration tab: desktop shortcut, applications-menu entry
  and command link, all unchecked initially, with ownership checks. Command
  locations come from the existing host PATH; exact/shared destinations are
  confirmed, protected changes use system authorization, and PATH is never edited.
- Details column-heading checklist and live heading dragging, retaining
  Preferences controls. Visibility/order are per panel, shared by its tabs;
  Preferences selects Left/Top or Right/Bottom and applies without restarting.
  Column widths remain per-tab.
- Preferences shortcut at the far right of the menu bar and a first-run
  welcome for new personal settings profiles.

### Fixed
- Restored the original built-in toolbars, command order and theme icon sizes.
  Removed experimental toolbar editors, presentation/context menus, overflow
  controls and size/display overrides. Existing Custom Actions and their saved
  settings remain intact, with menu/context access and the separate custom toolbar.
- The icon chooser's Browse button opens an image-file dialog rather than a
  folder-only dialog; the selected file is listed and previewed automatically.
- Custom toolbar editing uses the main toolbar's Settings icon at the far right
  with an "Edit Custom Actions" tooltip and no text.
- Custom action and folder icons fit the toolbar instead of expanding it to
  their original image size. Successful custom actions launch without an
  output window by default; failures show output, with an explicit option
  to show successful command output too.
- Closing after changing Details column visibility no longer stops before
  saving tabs and window layout. Widths are saved from each pane's actual
  columns, including reordered search-result columns.
- Tabs retain independent column widths across switching, reordering and
  restored sessions. Failed-unmount tab restoration preserves these too.
- Unmounted device names retain readable theme text and add "(unmounted)"
  instead of using disabled/inverted text.
- Preferences pages scroll by readable line-sized increments instead of single
  pixels; nested lists retain their own scrolling.
- Open Archive as Folder stays visible for supported archives even without
  archivemount installed (including ISO images). Missing-tool guidance offers
  to open the upstream installation page, without installing software.
- Failed unmounts restore the tabs closed for that filesystem, including their
  order, active tab and view modes. Busy errors identify visible processes
  where possible and ask users to close applications using the device.
- File-panel and search-results context menus offer separate Rename and Bulk
  Rename entries, enabled for one and multiple selected items respectively.
- Built-in search runs system tools with the host library environment, avoiding
  the portable `libselinux.so.1` version warning from `find`.
- Search windows and dialogs use the Swordfish application icon in both legacy
  window-manager hints and the modern taskbar/dock icon property.

### Changed
- Context menus group editing and rename together, followed by copy/move/link
  actions. Copy Path replaces Copy Name and copies plain full paths; Other
  Pane commands use the destination pane's active tab.
- A selected folder offers Open in New Tab in the file-panel context menu,
  opening an active tab in the same pane without replacing the original tab.
- Devices have separate stop/go Mount/Unmount and Eject buttons. Disconnected
  SMB/SFTP shares remain remountable for the session; eject forgets their
  sidebar entry without touching credentials. USB eject unmounts all drive
  partitions first and aborts on failure, without forced unmounts.
- The automatic portable build now runs for `swordfish-<version>` release
  tags and attaches the same zip, tar.gz, source and SHA256SUMS files as the
  hand-made releases. It never replaces files already on a release, and it
  no longer overwrites the project website.

## swordfish-0.0.2 — Xfe-independent stable release

Changes since `swordfish-0.0.1`. Full details are in
[PROJECT-HISTORY.md](PROJECT-HISTORY.md), section 3.18.

### Changed
- Swordfish no longer reads Xfe's system settings (`/etc/xfe`,
  `/usr/share/xfe`, `/usr/local/share/xfe`, `xferc`). An installed Swordfish
  uses only its own `share/swordfish` folder and `~/.config/swordfish`, so it
  cannot pick up or disturb an installed Xfe.
- Removed the inherited Xfe Debian packaging (`debian/`), which still built a
  package named `xfe`, and Xfe's `.run` installer scripts (`buildrun`,
  `installrun`, `uninstall-xfe`), which install or delete `/usr/local/bin/xfe`
  and `/usr/local/share/xfe`.
- The automount helper is now `swf-automount` instead of `xfe-automount`.
  Installing Swordfish no longer overwrites Xfe's `/usr/bin/xfe-automount`,
  and starting Swordfish no longer stops (or starts) Xfe's automounter.
  The helper reads its "open a window" option from Swordfish's settings and
  opens new drives in Swordfish instead of Xfe.
- Credits to Xfe, the FOX Toolkit and X Win Commander are kept in AUTHORS.

### Fixed
- Automount "open a window" now works for drives whose names contain spaces.

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
