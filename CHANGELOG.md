# Swordfish FM portable changelog

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
