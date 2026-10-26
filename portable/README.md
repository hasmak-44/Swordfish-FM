# Portable Linux build

The portable build produces a relocatable AppDir-style directory for Linux
x86_64. It includes the Swordfish programs, installed resources, and shared
libraries collected by `linuxdeploy`. Run `AppRun` from the generated folder;
do not run `make install` as root.

The build uses Docker to compile against Debian 10 (glibc 2.28), so the bundle
runs on Linux systems with glibc 2.28 or newer (for example Debian 10+,
Ubuntu 18.10+/20.04+, Linux Mint 20+, MX Linux 19+, Fedora 29+ and RHEL 8+).
Debian 10 is the oldest practical base: Debian 9 lacks the GLVND OpenGL
libraries the bundle relies on. Set `BASE_IMAGE` to build on another Debian
release, for example `BASE_IMAGE=debian:bookworm-slim`. It downloads a
checksum-pinned `linuxdeploy` release to collect shared libraries. Docker and
internet access are required to build:

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

## Local devices and automount

Mounted disks under `/media/` and `/run/media/` appear in Devices.
Connected, unmounted filesystems reported by the host `lsblk` also appear,
including internal partitions. These entries are grey but remain clickable
to mount and open them. Mount-table changes are polled, including disks
unmounted by another application. Unknown filesystems, locked encrypted
volumes and swap are not offered as new mountable volumes.

An action button at the right of a USB/hotplug device row safely removes it
(unmount, then power off). Mounted internal partitions and network shares
have an Unmount button instead. Root and unmounted internal partitions have
no action button. To unmount a USB drive without powering it off, use Unmount
in the toolbar or the device's right-click menu.

Toolbar Mount/Unmount requires an explicitly selected device or exact mount
point. With no selection (or only `..` selected), both buttons are disabled;
the current folder is not inferred as an unmount target. Root, Home and a
filesystem containing Home are protected from unmounting.

Mounting and unmounting desktop-managed disks requires the host UDisks2
service and `udisksctl`; system policy may ask for an administrator password.
Unmount leaves the connected drive available for remounting. Eject (Safely
Remove) additionally powers off supported hotplug drives; it is not enabled
for internal drives or network shares. The root filesystem cannot be unmounted
from Swordfish. A busy-drive error must be resolved before safe removal.

The optional automounter requires the host UDisks2 service and `udisksctl`.
Portable bundles include `xfe-automount` and its runtime libraries. The startup
check runs `udisksctl status` with the host environment and checks its exit status,
not translated column headings. A failed check reports the actual command output
and disables startup for that session without erasing the saved preference or
stopping an already running automounter.

## Standalone network discovery diagnostic

`network-discovery-diagnostic.py` is an experimental host-side diagnostic, not
part of the frozen RC4 application or its launcher. Run it from a normal desktop
terminal on the LAN, without `sudo`:

```sh
/usr/bin/python3 network-discovery-diagnostic.py --output discovery-report.json
```

When running from this source tree, use `portable/network-discovery-diagnostic.py`.
Python 3 and `ip` (iproute2) are required. Host Python GObject bindings
(`python3-gi`) and GVFS backends enable the GIO method; `nmblookup` (usually
`samba-common-bin`) enables NetBIOS. Missing optional components are reported,
not installed automatically.

The diagnostic runs three bounded methods concurrently: existing host-GVFS
virtual network roots, IPv4 WS-Discovery multicast probes, and local NetBIOS
broadcast queries followed by name lookups. It never mounts virtual roots or
shares, requests credentials, enumerates share contents, or enables SMB1.
Consequently, a GVFS root not already available is reported as an error rather
than activated. WSD/NetBIOS responders are candidates, not verified SMB servers.

After discovery, two follow-up checks run unless `--no-checks` is given (5 second
limit each). For WS-Discovery devices, the advertised metadata URL is queried with
a WS-Transfer Get (as Smb4K does) to read the Windows computer name and
workgroup/domain; only `http` URLs on the replying address are contacted. For
WSD/NetBIOS IPv4 addresses (at most 256), TCP port 445 is connected and closed
immediately to report `open`, `refused` or `no-answer`; no SMB data or login is
sent. WSD entries also record the reply time and whether the retry was needed.

Use `--interface eth0` (repeatable) to limit active WSD/NetBIOS probes; GVFS uses
the desktop's own interface selection. By default all active global IPv4
interfaces are probed, including VPN/container interfaces if present. Use this
only on networks you are authorized to probe. There is no address-range scan.
`--timeout 15` changes the per-method deadline (2-30 seconds; default 8).
The JSON report includes per-method availability, errors, elapsed times, raw
results and an exact-address summary. Hostname/IP aliases are not guessed or
merged. Empty results do not prove absence, and IPv6-only hosts are not covered.
Exit status 1 means no method completed successfully; 2 indicates an invocation,
interface or report-writing error. Status 0 does not guarantee any server found.

The output file must not already exist. Reports contain LAN names, addresses and
advertised metadata URLs, but no requested credentials. Review before sharing.
Offline tests: `/usr/bin/python3 tests/network-discovery-diagnostic-test.py`.

## Preview engines

The sidebar, Thumbnails view and Icons view share an asynchronous preview cache.
GdkPixbuf decodes raster images, libwebp handles WebP, LibRaw extracts the
embedded preview from camera RAW files (CR3, CR2, NEF, ARW, RAF, ORF, RW2, PEF,
DNG, SRW and others), librsvg renders SVG, Poppler renders
the first PDF page, and ffmpegthumbnailer/FFmpeg produces video stills. This does
not embed a document viewer or video player. Text sidebar excerpts remain bounded
to 8 KiB. Unsupported or failed previews do not create thumbnail placeholders.
Icons view keeps every file and shows previews in place of type icons (Preferences
> File List > Previews). Thumbnails view lists only files with a preview and the
status bar reports how many files it hides.

Decoding runs in the separate `swordfish-preview` process, one job at a time,
with a 12-second wall-clock deadline, 8 seconds of CPU time, 512 MiB address
space and a 16 MiB output limit. Requests are bounded and sidebar selections take
queue priority. These are reliability limits, not an OS security sandbox.
Generated PNGs are cached under `portable-data/cache/swordfish/previews`;
source identity, modification time, size and requested zoom size invalidate
old previews. Cache cleanup targets 128 MiB.

The bundle includes the worker, decoder libraries, GdkPixbuf loader modules,
MIME data and ffmpegthumbnailer. The launcher regenerates a relocatable loader
cache; loader settings are applied only inside the worker, not to host apps.
Bundled GIO uses its own module directory rather than loading incompatible
host GVFS modules; external commands restore the host GIO module setting.
SMB discovery uses the separate `swordfish-network` helper with the host GIO/GVFS
environment. In post-RC4 development builds, expand or click Network to discover
computers through GVFS/DNS-SD and WS-Discovery. Discovery never mounts anything
or requests credentials, and does not run at startup.

Single-click a discovered computer to list its shared folders in the active tab;
no new tab is opened, and clicking it again reuses the same tab.
The host helper's `--shares smb://HOST/` mode opens only the server's GVFS
browsing connection, not its file shares. It tries anonymous access first and
requests credentials only if the server requires them. Passwords travel over
the helper's private input channel, not command-line arguments or logs.
Double-click a shared folder to mount and open only that share. File operations
and drag/drop are unavailable in the server's share list; they work normally
after opening a share. Both browsing and mounting use the host libraries rather
than the portable bundle's GLib. These changes do not alter the frozen RC4
release. The host still needs a running desktop session and a GVFS SMB backend
(commonly `gvfs-backends`); missing/denied backends remain visible as errors.
Unmounting a GVFS share uses host `gio mount -u`, including from the file pane
and from inside a share's subfolder. The configured `mount`/`umount` commands
remain in use for ordinary filesystem mounts. A failed command cannot report
success, and a GVFS unmount must also remove its mount entry before success is
shown. The Devices list tracks mount additions/removals without retaining old
rows. When a share or local filesystem is unmounted in Swordfish, tabs under it
(including inactive tabs and subfolders in either pane) close before the
unmount command runs, so nothing in Swordfish keeps reading the disappearing
mount. Pending pointer releases are sent to the X server before mount commands
or navigation start, so a slow mount cannot hold the desktop's mouse input.
Shares unmounted by another file manager also have their tabs closed. If a pane loses its last tab, it opens one Home tab. Attempting to open
an already-mounted share reports its local mount path and reuses the mount
instead of issuing another mount command.
Third-party copyright notices, license texts and build package versions are in
`usr/share/doc` and `usr/share/common-licenses`. Video codec availability depends
on the bundled FFmpeg build. The portable download is larger with these engines.

Documents open with the host desktop's default application unless the user
has saved a Swordfish-only extension override. Under Programs, "Swordfish default
programs" shows the configured desktop application by name and offers installed
alternatives declaring support for the relevant MIME type. Finding an installed
application does not automatically select it. Text, image, PDF, archive, audio,
and video viewing follow system defaults unless explicitly overridden. Image
and PDF editing have no inferred default: select an editor or use Edit With.
The grouped entries use representative MIME types (plain text, PNG, PDF, ZIP,
MP3, and MP4); the per-extension list above shows exact file-type defaults.
These action preferences remain separate from extension associations.
Host XDG associations are read without modifying them.
File types are resolved using the host's shared MIME database, including when
the bundled extension entry has no MIME metadata. "Not configured" means that
the desktop query succeeded but returned no default; unresolved file types and
query failures have separate status messages. Built-in tools do not replace
missing system defaults.

Open With uses another application once unless "Always use this application
for .EXT files in Swordfish" is explicitly checked. Overrides are stored in
the portable configuration, match extensions case-insensitively, and preserve
recognized compound archive suffixes such as `.tar.gz` and `.tar.xz`.
Extensionless files cannot receive a saved association. Use system default
removes one override; Restore System Defaults removes all application overrides
after confirmation, leaving unrelated preferences and host settings unchanged.
Changes in Preferences are applied only on Accept.

Edit is distinct from Open: text uses the system default application unless a
text-editor override is saved. Images and PDFs use an explicitly configured
editor under Programs; otherwise Edit With asks the user to choose an installed
application. Installation or MIME support alone does not prove an application
can edit a file. Other types also ask rather than guessing or editing binary
data in the text editor. Edit With can save an extension-specific editing
choice, separate from normal Open associations and desktop defaults. Its saving
checkbox is unchecked each time. Missing editors are reported before asking.

Programs run without script confirmation; scripts use Always run, Ask (the
default), or Don't run for normal opening. An explicit Execute remains available
with confirmation when Don't run is selected. Script interpreter declarations
are respected rather than forcing every `.sh` file through the same shell.
Launching never silently changes executable permissions. Missing runtimes,
permissions, associations, and launch failures are reported explicitly.

Run from Terminal uses the desktop default terminal, unless a Swordfish-only
terminal override is configured, and keeps output visible until Enter is pressed.
Run as Administrator is restricted to one program or script at a time and uses
the system authentication service. Swordfish never collects or stores passwords
and does not elevate the entire file-manager session.

Pushing a tag named `SwordFish-Portable-*` or `swordfish-portable-*` builds the
portable folder, updates the direct download at
`https://hasmak-44.github.io/Swordfish-FM/Swordfish-Portable.zip`, and stores a
copy as a GitHub Actions artifact for 90 days. The workflow also publishes the
portable ZIP and SHA256 checksum as GitHub Release assets. RC tags are marked
as prereleases; use the explicit release link in the main README.

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
  - `listing-diagnostic.log` records folder loading (Detailed and Debug levels
    only).

Log entries include timestamps and process IDs. They record semantic actions
and relevant paths, not raw keystrokes or mouse movement. Coverage will expand
as more actions are instrumented.

The amount of logging is set in Preferences > Settings > Logging, which also
shows the log folder. The change applies immediately:

- **Off**: nothing is written.
- **Errors only**: only `errors.log`.
- **Normal** (default): all categories above except the listing diagnostic.
- **Detailed**: adds one summary per folder load (entries read, skipped and
  shown, errors and time taken).
- **Debug**: also logs every file in each folder load. This slows down large
  folders and produces big logs, so use it only while investigating a problem.

Logs can contain private file and folder names; review them before sharing.
`SWORDFISH_LISTING_DIAGNOSTIC=1` still forces the Debug level for one run.

The portable launcher leaves `HOME` unchanged, so file navigation still starts
from the user's real home directory. Terminal launches restore the host library
environment, including shells started by the internal terminal, so system
terminals and shell commands do not load incompatible bundled libraries.
Leave the Terminal field in Preferences empty to follow the desktop default.
Swordfish uses `xdg-terminal-exec` when available, Xfce's preferred-terminal
service on Xfce, or Debian's `x-terminal-emulator` alternative. These services
read the host's configuration, not the portable configuration. If no supported
service is available, Swordfish asks you to configure a terminal explicitly.
A command entered in the Terminal field overrides this choice only in Swordfish;
existing saved terminal commands are preserved.
The toolbar/menu terminal opens in the current folder (or the selected folder).
For `xfce4-terminal`, Swordfish passes the working directory explicitly and uses
its `--execute` argument convention for Run from Terminal, including search results.
Portable mode reads settings from the
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
loaded from the host. The bundle is built using glibc 2.28, but still cannot
guarantee compatibility with Linux systems older than Debian 10 or with every
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

## Mounted remote folders

Folders already mounted below `/run/user/<uid>/gvfs/` use the host's native
GIO/GVFS backend through `swordfish-network --list`, with asynchronous batches
of 100 entries. Details, Places and Tree consume returned metadata instead of
requesting it separately for each file through FUSE. The busy cursor remains
active while the request is pending; navigating away or collapsing a branch
cancels requests that no other view needs. Existing rows and selections remain
visible during refresh. Cached active listings refresh every 30 seconds, and
the normal Refresh command requests an immediate reload.

The host must have the corresponding GVFS backend installed and the share
already mounted. Failure to resolve the native remote URI is reported explicitly;
Swordfish does not silently fall back to per-entry FUSE enumeration, mount a
share, or request credentials. Local paths and non-GVFS mounts retain the
existing loader. The unfinished Network discovery section is temporarily hidden
from the sidebar. Mounted shares remain under Devices, and Connect to Server
remains available. This change does not add network service discovery protocols.

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
