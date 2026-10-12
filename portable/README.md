# Portable Linux build

The portable build produces a relocatable AppDir-style directory for Linux
x86_64. It includes the Swordfish programs, installed resources, and shared
libraries collected by `linuxdeploy`. Run `AppRun` from the generated folder;
do not run `make install` as root.

The build uses Docker to compile against Debian 12 (glibc 2.36), a compatible
baseline for older Linux systems than the Ubuntu 24.04 environment used by
the first prototype. It downloads a checksum-pinned `linuxdeploy` release to
collect shared libraries. Docker and internet access are required to build:

```sh
./portable/build-portable.sh
```

The output is `build/Swordfish-Portable`, with a compressed archive at
`build/Swordfish-Portable.tar.gz`. The folder can be copied directly and
started with `build/Swordfish-Portable/AppRun`. The `run-swfa`, `run-swfi`,
`run-swfp`, and `run-swfw` launchers start the corresponding companion apps.
The bundle also provides the original Xfe executable names (`xfa`, `xfi`,
`xfp`, and `xfw`) as compatibility aliases for saved settings and file
associations.

Preferences under Programs offers three application modes: use the host
desktop's default applications for every file, use Swordfish's built-in
applications where available (and the desktop defaults for other files), or
customize each program and file association individually. The default is to
use desktop defaults. Host XDG application associations are used without
moving Swordfish's own configuration out of the portable sidecar.

Pushing a tag named `SwordFish-Portable-*` or `swordfish-portable-*` builds the
portable folder, updates the direct download at
`https://hasmak-44.github.io/Swordfish-FM/Swordfish-Portable.zip`, and stores a
copy as a GitHub Actions artifact for 90 days. The workflow deploys the download
using GitHub Pages and does not publish a GitHub Release; tag pages continue
to show GitHub's automatic source archives.

For user download and launch steps, see the main
[README](../README.md#download-and-start-swordfish).

Writable state is kept in `portable-data` beside the AppDir:

- `config`, `data`, `cache`, and `state` are the portable XDG directories.
- `scripts` is the custom-script folder used by Swordfish.
- `themes`, `icons`, and `addons` are writable locations reserved for
  portable customizations and future extensions.
- `logs` contains persistent, categorized application logs:
  - `application.log` records startup and shutdown.
  - `ui.log` records layout, panel, tab, hidden-file, and thumbnail changes.
  - `file-operations.log` records file opens, navigation, and requested
    copy/move/rename/link/trash/delete operations.
  - `settings.log` records saved program and panel preferences.
  - `errors.log` records selected operational failures, such as unavailable
    associated programs or denied directory access.

Log entries include timestamps and process IDs. Logging is enabled
automatically; it records semantic actions and relevant paths, not raw
keystrokes or mouse movement. Coverage will expand as more actions are
instrumented.

The portable launcher leaves `HOME` unchanged, so file navigation still starts
from the user's real home directory. Portable mode reads settings from the
sidecar, uses resources from its own `usr/share/swordfish`, and does not load
installed XFE or Swordfish registry defaults. Icon themes can be selected from
the bundled resources or from folders under the sidecar `themes` and `icons`.
Installed system icon themes (freedesktop layout, from `/usr/share/icons`,
`~/.local/share/icons` and the host data directories) are listed as
"Name (system)". Choosing one converts the icons Swordfish needs to PNG with the
bundled `rsvg-convert`, using `usr/share/swordfish/icons/system-icon-map.txt`
to map names, and stores the result in `portable-data/icons/<theme>-system`.
It is converted once and works without the system theme afterwards; delete the
folder to convert again. Icons the theme lacks keep the default icon.
The built-in color themes also include Charcoal, Forest Green,
Forest Green 2, Gray On Blue and Gray-Blue. The additional bundled icon themes
are Adwaita, Faba, HighContrast, Human, Moka, Numix, Numix-Circle, Obsidian-Amber,
Obsidian-Aqua, Obsidian-Green, Papirus, Tango, ePapirus, ePapirus-Dark,
gnome-brave, gnome-dust and gnome-human. These are already converted to
Swordfish's PNG layout and do not need the original system themes installed.
Their bundled folder names omit the conversion cache's `-system` suffix.
The current program supports custom scripts and configurable icon paths; the
`addons` directory is only a reserved location, not a plugin loader.

`linuxdeploy` bundles most application libraries, including FOX, image and
archive libraries. Core system libraries and interfaces such as glibc, the C++
runtime, X11, font handling, and graphics drivers remain host-provided. The
generic GLVND OpenGL dispatch libraries are bundled, while graphics drivers are
loaded from the host. The bundle is built using glibc 2.36, but still cannot
guarantee compatibility with Linux systems older than Debian 12 or with every
graphics stack. Test the folder on the target systems before relying on it.

The first build is not yet an AppImage; it creates the relocatable directory
so its isolation can be tested before adding the AppImage packaging step.

## Pane navigation

Each file pane has its own Back, Forward, history drop-down, and Up controls,
with Home, Root, and Desktop shortcuts on the right of its path bar. These
controls navigate their own pane and synchronize the shared tree with it.
The Desktop shortcut uses the desktop folder configured by the host; it
does not create a folder if that location is missing.

Click the edit-path button or the empty area after the breadcrumb buttons to
type a path. Ctrl+L edits the active pane's path (the shortcut remains
customizable in Settings). Enter navigates to the folder and returns to
breadcrumbs; Escape cancels editing without changing folders. Relative
paths, `~/` paths, local file URIs, `smb://server/share` addresses, the
common `smb//server/share` typo, and Windows-style `\\server\share` paths are
supported. For SMB servers, typing only `smb://server` opens a share picker.
Invalid or inaccessible folders show an error rather than silently navigating
somewhere else.

The edit-path button sits just left of the path, after the navigation
controls. Vertical separators distinguish navigation, the path, and places
shortcuts. Address rows and panes have visible borders, with a contrasting
resizable divider between the two panes in either orientation.

The existing global toolbars remain available during the pane redesign.

Below its address bar, each pane has its own horizontally scrollable tab bar, with a
plus button immediately after the last tab, close buttons,
and a tab context menu for reordering. Menu commands and the New Tab shortcut
operate on the active pane; opening a tree location in a new tab uses that
pane as well. Closing the last tab leaves the pane at its current folder.
Click empty tab-bar space to open a new tab, or right-click it for New Tab.
The existing “Always show tab bar” preference applies to both pane bars.

Settings → Settings → Address Bars selects separate pane bars (the default),
or a common bar following the active pane and its selected tab. Selecting the
common bar hides both pane bars; selecting separate bars hides the common bar.
Both modes support breadcrumb buttons and editable paths.

New Tab, Close Tab, Close All Tabs in This Pane, New Window, New Root Window,
and Connect to Server are in the File menu rather than Tools.

When “Restore tabs on startup” is enabled, each pane saves and restores its
own paths and selected tab. Previously saved shared tabs migrate to the left
pane. Explicit startup folders take precedence over restored tabs.
Restarting to apply preferences (including color or icon themes) preserves both
panes' current folders, tabs, selected tabs and active pane even when normal
startup tab restoration is disabled.

The Bookmarks menu now focuses on bookmark and tab-group actions: bookmark the
current tab, bookmark selected folders, save all current tabs as a named tab
group, reopen saved tab groups, and open a bookmarks manager. The manager can
open, edit, reorder and remove bookmarks, and rename or remove tab groups.
Dragging a tab from a pane tab bar, or a folder from a file list, onto the
Bookmarks list in the Places pane adds a bookmark for that folder.

## Open With

The Open With drop-down lists recently used commands, followed by the host's
installed desktop applications (from the host `applications` folders in the
XDG data directories). Choose an application by name, type a command, or use
the browse button to select an executable that is not listed.

The browse (file selection) dialog has a resizable sidebar on the left:

- **Recent** – folders where files were last chosen in Swordfish dialogs,
  followed by folders of recently used files from the host desktop
  (`recently-used.xbel`).
- **Places** – Home, Desktop, Documents, Downloads, Music, Pictures, Videos
  (from `~/.config/user-dirs.dirs`, only if they exist), File System and the
  Swordfish bookmarks.
- **Devices** – mounted partitions, USB drives and CD/DVD media (`/media`,
  `/run/media`, `/mnt` and other mounted disks) plus mounted network shares
  (SMB/CIFS, NFS, SSHFS and GVFS). Unmounted drives are not listed yet.

Click an entry to open that folder; hover it to see the full path.

## Sharing color themes

In Settings → Appearance, custom color themes can be exported to a
`*.swordfish-theme` text file (name, tip and 12 `#RRGGBB` colors) and imported
on another machine with the Import/Export buttons or the theme context menu.
