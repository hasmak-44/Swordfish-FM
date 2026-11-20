# Portable Linux build

Current latest release: **0.0.3** (`swordfish-0.0.3`), a stable release.
Extract into a new folder and back up existing `portable-data` before
migrating it. Release archives do not include a settings profile or test data.

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

## Alternative app icons

The `app-icon` folder contains alternative Swordfish icons for menus,
launchers or docks: transparent PNG files, plus JPG files with a white or
dark background. A normal installation places them in
`share/swordfish/app-icon`.

## User Manual, welcome and optional shortcuts

Choose **Help > User Manual** for the bundled, illustrated beginner guide.
It opens in your browser and works offline; the HTML file is
`usr/share/swordfish/manual/index.html`. Its screenshots describe the matching
0.0.3 interface. The same HTML and images can be hosted online without
maintaining a separate manual.

A new personal settings profile shows a welcome with manual access, then
optional customization. Existing settings are not treated as a first run.
The Preferences button at the right of the menu bar opens the same settings
dialog as **Edit > Preferences**.

## Theme Editor and Preferences

Preferences shows scrolling categories on the left and the selected category's
tabbed settings on the right. **Appearance > Theme** selects and manages
themes. Choose **Edit**, **New theme** or **Duplicate and edit** to open the
separate Theme Editor. Its safe mockup is a display-only copy of the current
file-manager view.

Editor **Apply** changes only the mockup; **Save** persists and activates all
cumulative edits. This differs from Preferences **Apply**, which saves the
pending Preferences settings and keeps that dialog open. Dialogs and child
windows use a fixed readable palette; the main file-manager window and editor
mockup remain themed.

The **System > System integration** page offers a desktop shortcut, applications-menu
entry and an optional `swordfish` command link. All start unchecked. Accept applies
changed integration choices; Cancel does not. Saving unrelated preferences
leaves the launchers alone. If an owned launcher has been modified, the recorded
choices stay visible but integration controls are disabled with a warning;
other preferences can still be saved. Launchers for this portable build use `AppRun`,
not the bare binary. Only unchanged integration items created by Swordfish
are removed when unchecked; unrelated existing items are never overwritten.
Moving the portable folder breaks its shortcuts; from the new location,
uncheck the affected choices and Accept, then recheck them and Accept to
recreate them. Desktop environments may additionally require
**Allow Launching**. The command destination is selected from the existing host
PATH, with writable locations preferred. The full destination and whether it is
shared or requires administrator authorization are shown before applying.
Swordfish never modifies PATH. A missing `~/.local/bin` is offered for creation
only if already listed in PATH. Protected standard system destinations require
explicit system authorization through `pkexec` to run the bundled or installed
non-setuid `swordfish-command-link` helper. Shared command links are available
to other users; desktop/menu shortcuts remain personal. Unchecking a protected
link also requires authorization. Cancellation or an unsafe helper/destination
reports an error and leaves Preferences open.

In Details view, right-click a column heading to show/hide columns, or drag a
heading onto another heading to change their order. Name remains first and
visible. Search results retain their Folder Name column. Trash uses the fixed
Caja-style columns Name, Size, Type, Trashed On and Original Location.
Existing Preferences column controls remain available
with a Left / Top Panel or Right / Bottom Panel selector. Visibility and order
are shared by every tab in that panel; the other panel is independent.
Preferences and the heading menu edit the same choices immediately, without
a restart; Cancel discards pending Preferences edits. Column widths remain
per-tab, including when saved tabs are restored. Existing Beta 1 per-tab
layouts migrate using each panel's previously active tab.

On Linux, **Trash** combines the host's Home Trash with the current user's
standard Trash folders on mounted drives (`.Trash/<uid>` under a sticky
directory, or `.Trash-<uid>`). This includes items trashed by other file
managers. The address bar retains the Home Trash folder as its navigation
anchor; **Original Location** distinguishes same-named items from different
drives. **Restore Selected Items** uses each item's own restore information.
**Trash Size** and confirmed **Empty Trash** apply to the combined view.
Unmounted drives are unavailable until mounted again. Contents left in an
older portable bundle's private `portable-data/data/Trash` are not moved or
deleted automatically and are not part of this combined view.

**Copy Path** copies full paths as text. The context menu groups
editing/rename separately from copy/move/link actions; Other Pane commands
use the other pane's active tab.

## Custom actions and the separate custom toolbar

Open **Custom Actions → Edit Custom Actions…** or **Tools → Custom Actions…**
to create commands of your own. Preferences also has a **Custom Actions** page
opening the same editor. The top-level Custom Actions menu lists configured
actions and groups; unavailable entries are disabled. The editor
offers **Add Action…**, **Add Folder…**, **Add Separator**, **Edit…**, **Remove**,
**Move Up**, **Move Down**, and **Move to Folder…**. Folders can contain folders
and actions, producing cascading **Custom Actions** context menus. Removing a
folder also removes its children after confirmation. On save, a removed entry's
folder, including any scripts or other files in it, is deleted. Empty groups remain
visible in the editor but are hidden from menus and the custom toolbar.

The editor uses an icon/name/description tree with compact side controls.
Action properties have **Basic** and **Appearance Conditions** tabs, command
browsing, an icon preview, and placeholder help.
Each action has one **Command** field: the program followed by its arguments
and placeholders, as typed in a terminal, for example `gimp %F` or
`wine "/path/to/application.exe"`. Quote a program path that contains spaces.
Browse selects a program or script and inserts its quoted path; add arguments
after it. **Working directory** defaults to the active folder when empty;
Browse selects another folder. It accepts `%p`, `%o`, `%d`, `%f`, `%a`, `%r`, and `%%`
without shell evaluation. Relative paths resolve from the active folder.
Actions saved by earlier test builds with separate Program and Parameters
fields open as one equivalent command. They are normally saved unchanged unless
edited, except when portable script references are migrated as described below.

**Parameter Legend…** opens scrollable
help. **Test Action** checks placeholder expansion, working directory and
shell syntax without running the
action. It shows Checks Passed or an explanatory error. It cannot verify an
application's supported options or guarantee runtime success; executable
names in the command are not resolved by the syntax checker.
Actions have a name, description, optional PNG icon, command, file/folder and
selection conditions, and semicolon-separated filename patterns. Use `*` for
all files, or `.jpg;.png;.tar.gz` as shorthand for `*.jpg;*.png;*.tar.gz`.
Explicit wildcard patterns remain supported; matching is case-sensitive and
every selected file must match. Folders are eligible independently of file
extensions when **Folders** is checked; this also makes the current folder
eligible with no selection. The prominent **Show without selection** option
separately allows background actions without applying selection conditions.
Selection range remains unrestricted (`*`).
Optional confirmation, **Show command output**, and terminal execution are
available. Normal actions run without an output window on success; a failure
opens the captured output. Enable **Show command output** when you want to
inspect successful output too. Only run commands
you understand and trust; actions do not automatically request root access.

Use the **Insert** chooser for placeholders: `%f`/`%F` first/all selected paths,
`%n`/`%N` basenames, `%d`/`%D` parent directories, `%u`/`%U` local file URIs,
`%p` current pane directory, `%o` opposite pane directory, and `%%` literal `%`.
`%a` is the action's own folder after it has been saved; `%r` is the portable
data folder. Both also work in the working-directory field.
Additional codes: `%c` actual selected-item count, `%w`/`%W` first/all basenames
without the last extension, `%x`/`%X` first/all last extensions without dots.
For `archive.tar.gz`, these are `archive.tar` and `gz`; `.hidden` has no
extension. Empty extensions remain empty arguments. `%c` is zero on the
background even when Folders supplies the current folder.
Paths are quoted automatically. List placeholders must be separate, unquoted
arguments. With no selection, `%d`/`%D` mean the current directory. For an action
with **Folders** enabled, selected-path/name/URI placeholders use the current
folder too; otherwise they report that a selection is required. For complex
shell logic use a script.

Check **Show on custom toolbar** for individual actions (on the
**Appearance Conditions** tab) or folders (on the **Basic** tab). A checked
folder exposes its descendants as a dropdown; checked descendants are not
duplicated outside it. Separators divide toolbar groups. Unavailable context
actions are hidden; toolbar actions are disabled. Context submenus always
include an editing entry, even with no configured/applicable actions.
**Tools → Show Custom Toolbar** toggles this independent toolbar; its far-right
**Settings** button opens the editor with an **Edit Custom Actions** tooltip.
Custom-action toolbar contents remain separate from the built-in toolbars.
In **Preferences → Appearance → Toolbar → Tool Bar Icon Size**, choose the **16 px**,
**24 px** or **32 px** radio button for the custom toolbar. Sizes scale with
display DPI. The change applies when Preferences is accepted; Cancel discards
it. Image and system icons are loaded at the selected size, keeping their
proportions; the result depends on the quality of the source image. Menus and
the built-in toolbars keep their normal sizes.
Click the icon preview or its Browse control to open **Choose an Icon**.
The maximizable chooser displays installed system icons in a labeled grid
that reflows when resized. Select a category (actions, applications, devices,
file types, locations, and others) or **All Icons**, then search within it.
Categories follow the installed theme's directory context, with standard
directory names as a fallback and **Uncategorized Icons** for other icons.
The grid loads previews in pages instead of decoding every system icon at once.
**Image Files** opens the normal file chooser, with folder navigation, a filename
field, image/all-files filters, and a live preview below the left sidebar.
There is no intermediate image-browser dialog. **No Icon** uses the default.
System selections store an
icon name and resolve it against host themes, inherited themes and hicolor;
portable mode does not restrict this to bundled icons. Startup looks up the
configured icon names together; the full catalog is built when the chooser
opens. When the main editor is saved, image selections are copied to
the action's own folder under `portable-data/custom-actions/`; a group icon is
stored in the group's folder. The original image is not changed, and filename
collisions are resolved inside the action folder. The folder tree and its
`action.ini`/`group.ini` settings move together with the portable profile.
PNG, JPEG and XPM are supported; SVG uses `rsvg-convert` and a converted-image
cache. Image files have thumbnails and a larger selected-image preview. A named
system icon depends on the target host's icons.

Custom actions are stored as folders in `portable-data/custom-actions/`.
Each action folder contains `action.ini`; directories without an action file
are groups and can contain nested groups and actions. Folder names are their
menu labels. An optional `group.ini` stores group presentation and ordering.
The editor records ordering; unpositioned manually added entries appear after
ordered entries, alphabetically. Swordfish checks this tree while running and
notifies you when it finds new actions. You can add or remove action folders
directly; changes are loaded automatically. The hidden
`.swordfish-storage` and `.swordfish-known-actions` files are managed by
Swordfish and should not be edited.

On first startup after upgrading, legacy definitions in `config/swordfish/xferc`
are copied into this folder tree. The original `CUSTOM ACTIONS` registry data
is retained as a backup and is not updated by later editor saves. Available
legacy image icons are copied into their action folders. Missing or unsafe
legacy icon paths are omitted with a warning; the action definitions remain.

Custom Actions never copy or rewrite scripts. The command, program and
working directory are saved and run exactly as entered, including commands
edited by hand in `action.ini`, so a script keeps its own location and any
relative paths it relies on. If the script or Swordfish is moved, update the
command, or the action reports that the script or command was not found.

To keep a script with the portable profile, put the script and any files it
needs in the action folder and refer to it with `%a`, for example
`sh "%a/process.sh" %F`, or place it in a shared folder and use
`sh "%r/scripts/process.sh" %F`. Interpreters, installed programs, and any
dependencies outside portable-data must exist on the target machine.

The built-in **General**, **Tools** and **Panel** toolbars use their original
commands, order and theme icon sizes. Experimental toolbar customization,
right-click presentation menus, built-in toolbar size/display preferences and
overflow controls have been removed. Older toolbar-customization settings are
ignored; saved Custom Actions are preserved. Manage custom toolbar contents
through the Custom Actions editor, not the built-in toolbars.

Properties **Apply** changes the editor's working copy; **Save** persists it.
Editor **Cancel** discards unsaved edits. Settings belong to Swordfish's own
personal or portable profile. Action-specific shortcuts are not included in
this stage.

**Duplicate Action** copies the selected action beside its source with a new
identity, preserving its settings and group. **Use Program Icon** suggests the
matching installed application's icon; it is an explicit choice and does not
silently replace a custom icon. Unsaved changes in properties or the main editor
are protected on Cancel, Escape and window close: choose **Keep editing** or
**Discard**. Unchanged dialogs close without that prompt. Import/export and
action shortcuts remain planned for later.

## Local devices and automount

### Opening archives as folders

For a single supported archive, the file-panel context menu offers **Open
Archive as Folder**, even when the optional host tool `archivemount` is missing.
If it is missing, Swordfish explains how to install it using your distribution's
software manager and offers to open the
[upstream installation page](https://github.com/cybernoid/archivemount#installation).
The browser opens only after confirmation; Swordfish does not download packages
or run privileged installation commands. Working host FUSE support is required.
Normal archive extraction does not require `archivemount`.

### Device controls

Mounted disks under `/media/` and `/run/media/` appear in Devices.
Connected, unmounted filesystems reported by the host `lsblk` also appear,
including internal partitions. Their names are dimmed with muted, theme-aware
text, but remain clickable to mount and open them. Mounted entries use normal
text. If a theme's colors cannot be muted while maintaining readable contrast,
the normal text color is retained and an **(unmounted)** suffix identifies the
entry instead. Mount-table changes are polled, including
disks unmounted by another application. Unknown filesystems, locked encrypted
volumes and swap are not offered as new mountable volumes.

Android and other MTP devices advertised by the host desktop also appear in
Devices before they are mounted. Open the device or use its mount button to
connect it. A waiting dialog stays open while you unlock the phone, select USB
file transfer and allow access. It opens the phone when its folder is ready.
Cancel stops retries and waits for an active GVFS mount request to finish. If
that request mounts the phone, Swordfish tries to undo its own mount; it does
not unmount an already-mounted device. The connected phone remains listed for
another attempt. Some GVFS backends cannot abort a mount immediately: if
cancellation cannot be confirmed, Swordfish warns that the phone may still
mount, and the helper continues trying to undo a late successful mount.
Mounted MTP devices can be unmounted from Devices. This requires the host
GVFS MTP backend, its volume monitor and GVFS FUSE support (commonly supplied
by `gvfs-backends` and `gvfs-fuse`), as well as a desktop session bus.
Swordfish uses the host's GVFS/libmtp stack rather than accessing the phone
directly. Devices not advertised by that stack, or using another USB protocol,
are not detected by this MTP mechanism. Mount errors are shown to the user;
host monitor/backend errors are recorded in the application log.

iOS devices are discovered through the host GVFS AFC volume monitor and appear
in Devices when that monitor exposes an available file-sharing volume. Unlock
the iPhone or iPad, connect it over USB, and approve **Trust This Computer**
when prompted. This requires the host's GVFS AFC support and libimobiledevice,
as well as the desktop session bus and GVFS FUSE support. Swordfish relies on
the host's AFC implementation; it does not implement the iOS protocol itself.

AFC does not provide general access to the iOS filesystem. Typically, only
documents belonging to apps that enable file sharing are visible, and the
available content depends on the iOS version and host GVFS backend. It is not
a replacement for a full-device backup or a guarantee of photo access. If the
device is locked, untrusted, or unsupported by the host backend, it may not
appear or mounting may fail; host monitor/backend errors are recorded in the
application log.

Host backend warnings appear in Devices with details on hover. While waiting,
temporary mount failures are recorded in the application log; unsupported
backends and other unrecoverable errors are reported in an error dialog.

Device rows have separate controls at the right:

- The pause button unmounts the selected filesystem. When it is unmounted but
  still available, this becomes a play button that mounts it again.
- The eject button safely removes a supported USB/hotplug drive. It unmounts
  all filesystems on the physical drive before powering it off, not just
  the selected partition. The drive disappears from Devices when the system
  removes it. Internal partitions do not have an eject button.

These play, pause and eject symbols are flat outlines: black on light
backgrounds and white on dark ones. The same symbols are used for Mount,
Unmount and Eject in device menus and the Tools menu/toolbar. Close Archive
Folder keeps its own icon.
- Root and other protected mounts have neither action.

Supported GVFS network shares (SMB and SFTP) stay in Devices after unmounting,
using the same muted text (or the **(unmounted)** suffix if it cannot be muted). They can be remounted
through the Mount button or by opening the entry.
These disconnected entries are remembered for the current Swordfish session.
Eject disconnects the share and removes its retained entry, without deleting
or changing saved credentials. Connect to Server can add the share again.
Remount uses the existing connection/authentication flow; a password prompt
is still possible if the desktop has no reusable credentials.

Toolbar Mount/Unmount requires an explicitly selected device or exact mount
point. With no selection (or only `..` selected), both buttons are disabled;
the current folder is not inferred as an unmount target. Root, Home and a
filesystem containing Home are protected from unmounting.

Mounting and unmounting desktop-managed disks requires the host UDisks2
service and `udisksctl`; system policy may ask for an administrator password.
Unmount leaves the connected drive available for remounting. Eject (Safely
Remove) additionally powers off supported hotplug drives. A busy-drive error
must be resolved before safe removal: Swordfish does not force-unmount or
terminate other applications. Failed unmounts keep the mount visible.
Tabs closed by a failed unmount are restored in both panels, with their original
order, selected tab and view modes, if that filesystem is still mounted.
Busy errors list applications and process IDs detectable through Linux `/proc`
(open files, working directories and executables), and ask you to close all
applications using the device and try again. Permissions or other types of
usage can prevent identification; the message says so if no application is
found. Swordfish does not request administrator access for this check.
If some partitions have already unmounted before another fails, their
individual states are updated, but the drive is not powered off.

The optional automounter requires the host UDisks2 service and `udisksctl`.
Portable bundles include `swf-automount` and its runtime libraries. It is
named differently from Xfe's `xfe-automount`, so Swordfish never starts or
stops Xfe's automounter. The startup
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
environment. When one portable copy launches another, the new launcher preserves
the already-recorded original desktop environment, including empty host library
and GIO module settings. Otherwise the older bundle's private libraries/modules
can be mistaken for host settings, breaking discovery and network-share unmounting.
Launcher regression checks run with `python3 tests/portable-launcher-regression.py`.
In post-RC4 development builds, expand or click Network to discover
computers through GVFS/DNS-SD and WS-Discovery. Discovery never mounts anything
or requests credentials, and does not run at startup. After you have searched
once, a change of network connection (Wi-Fi joined or switched, cable plugged
in) starts a new search by itself once it has settled for a few seconds, while
Network is expanded. Refresh keeps earlier results, greyed, until the new search
finds them again; a finished search removes entries it no longer finds, while a
stopped or timed-out search leaves them greyed. Hover the status line to see the
network interfaces searched; IPv4 interfaces without a link (for example an idle
Docker bridge) are skipped. IPv6-only devices are not discovered.

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
mount. Queued or running thumbnail previews for files on it are
cancelled too, so they cannot keep the drive busy. Background folder-size scans are stopped,
and Swordfish waits up to three seconds for them to let go of the drive. If
UDisks still reports the drive busy, Swordfish retries once after 1.5 seconds
before showing the error. Pending pointer releases are sent to the X server before mount commands
or navigation start, so a slow mount cannot hold the desktop's mouse input.
Shares unmounted by another file manager also have their tabs closed. If a pane loses its last tab, it opens one Home tab. Attempting to open
an already-mounted share reports its local mount path and reuses the mount
instead of issuing another mount command.

Network discovery also lists other devices that announce themselves on the
local network (UPnP/SSDP, Zeroconf/mDNS and WS-Discovery; nothing is
port-scanned): media servers, FTP and SFTP servers, printers and routers /
access points, each kind under its own heading (Media servers, File servers,
Printers, Routers / access points; computers sharing files get a heading only
when other devices are also listed). Devices on this computer itself are not
listed. Clicking a
media server browses its folders read-only (`dlna://` paths in the address
bar); double-clicking a file streams it to the audio or video player set in
Swordfish, else to the desktop's default application for the file type
(`mimeapps.list`), else to the browser through host `xdg-open`. Right-click a
file for **Play**, **Play With** (the default application, plus installed
players or viewers that declare the type and accept URLs, **Web Browser** and
**Other Application...**, which opens the regular Open With dialog) and **Copy
Stream Address**. An application remembered from that dialog becomes the
double-click choice for the extension. **Download...**
saves copies, one at a time in the background, never overwriting existing
files; a progress window shows the file, bytes and a bar, and **Cancel
Downloads** stops the queue and removes the unfinished `.part` file. FTP/SFTP entries connect
through GVFS like typed addresses. Printers and routers open their own web
page in the browser; printers also offer **Printer Settings on This
Computer** (`system-config-printer`, or the CUPS page). Found media servers
are remembered so restored `dlna://` tabs work before the next search.
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

Pushing a release tag such as `swordfish-0.0.3` (or an older-style
`swordfish-portable-*` / `SwordFish-Portable-*` tag) starts the
`portable-release.yml` workflow. It builds the portable folder in the Debian
10 container and packages `Swordfish-Portable-<version>.zip`,
`Swordfish-Portable-<version>.tar.gz`, `Swordfish-FM-<version>-source.tar.gz`
and `Swordfish-<version>-SHA256SUMS`, without `portable-data`. A
`swordfish-<version>` tag must match the version in `configure.ac`.

If the GitHub release does not exist yet, the workflow creates it, using the
matching `CHANGELOG.md` section as release notes. Versions containing `rc`,
`test`, `preview`, `beta` or `alpha` become prereleases. It then attaches the
files. A release that already has any of these files is left unchanged. The
same files are kept as a GitHub Actions artifact for 90 days. A build can be
rerun for an existing tag from the Actions page with "Run workflow".

The project website is built from the main README, which links to the latest
release; use the explicit release link in the main README.

For user download and launch steps, see the main
[README](../README.md#download-and-start-swordfish).

Writable state is kept in `portable-data` beside the AppDir:

The bundle creates empty user-content folders when built; the launcher also
creates any missing folders on first or subsequent runs without replacing
existing files. Release archives without a profile get these folders on launch.

- `config`, `data`, `cache`, and `state` are the portable XDG directories.
- `config/swordfish/xferc` is the writable settings file. Existing custom
  action definitions in its `CUSTOM ACTIONS` section are retained as a
  migration backup.
- `custom-actions/` contains the live custom action and group folder tree.
- `scripts` is the custom-script folder used by Swordfish.
- `themes`, `icons`, and `addons` are writable locations reserved for
  portable customizations and future extensions.
- `icons/custom-actions/` remains available for existing portable icon
  references; new custom-action images are stored beside their action in
  `custom-actions/`.
- `textures`, `patterns`, and `images` hold shared user graphics; custom color
  themes and their theme-specific graphics live under `themes/colors/`.
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

To copy a profile, close every Swordfish window using both the source and the
destination. Back up the destination, then replace its complete `portable-data`
folder with a copy of the source folder rather than merging them. This is the
reliable way to copy a consistent set of settings and other files. Action
definitions, scripts stored inside portable-data and image icons move with it. Bookmark destinations,
saved tabs, last-visited folders and bookmark or file-association icons inside
portable-data use relocatable references. External destinations and other saved
file paths, named system icons and external programs still depend on the target
machine.

For older profiles, an absolute bookmark or tab location is rebased only when
its old location no longer exists, so existing external folders are not
redirected. To make those references portable before copying while the old
location is still available, start the updated source copy, open each saved tab
group, and quit normally.

If a running portable window detects that its `xferc` settings file was changed
externally, including by another Swordfish window, it stops reading and saving
that window's stale settings and offers **Restart Now**, **Quit Without Saving**
or **Keep Running**. Restart opens a fresh window with the changed settings;
if it cannot start, the current window stays open and reports the error. Keeping
the window open does not re-enable settings saves; custom-action refresh and
saves also wait for a restart so an old editor cannot overwrite copied actions.
any unsaved settings in that window are lost when it closes. If a warning names
a preserved settings file,
keep it until you have checked which version you need. If the filesystem lacks
the operations needed for a safe settings save, an error is shown instead of
overwriting the file; move the profile to a filesystem that supports them.
An interrupted save can also leave a temporary or backup settings version and
trigger a warning on the next launch. Close all windows, compare the named file
with `xferc`, keep or restore the desired version, and move the leftover file
out of the settings folder before starting again.

This protection covers the main settings file, not an atomic snapshot of all
profile folders. Older versions do not have it. Close both copies before
copying even when this protection is available.

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
The selection uses a relocatable reference, so a copied profile keeps its
selected user theme or bundled theme when that theme is present in the new
bundle. Older portable icon paths are migrated when recognizable; an
unavailable theme is reported and the default is selected.
Installed system icon themes (freedesktop layout, from `/usr/share/icons`,
`~/.local/share/icons` and the host data directories) are listed as
"Name (system)". Choosing one converts the icons Swordfish needs to PNG with the
bundled `rsvg-convert`, using `usr/share/swordfish/icons/system-icon-map.txt`
to map names, and stores the result in `portable-data/icons/<theme>-system`.
It is converted once and works without the system theme afterwards; delete the
folder to convert again. Icons the theme lacks keep the default icon.
The built-in color themes also include MX Blues (first in the list), Charcoal,
Forest Green, Forest Green 2, Gray On Blue and Gray-Blue. The additional bundled icon themes
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

For local folders, the default address bar is an editable text field. The
pencil button to its left switches between text and clickable folder buttons
(breadcrumbs) for both panes, and saves the choice. The same choice is in
Edit > Preferences > General > General Options under **Default address bar
mode**, with **Button mode (breadcrumbs)** and **Edit mode (text)** choices.
Changing it in Preferences prompts for a restart.

The raised arrow button beside each pane's Back button opens a list of
previous folders; it is greyed out until there is history. The top toolbar's
Back and Forward history arrows use the same larger raised style. In breadcrumb mode, click the empty area after the
folder buttons to edit a path temporarily. Ctrl+L also edits the active pane's
path (the shortcut remains customizable in Settings). Enter navigates to the
folder; Escape cancels editing without changing folders. Relative paths, `~/`
paths, local file URIs, `smb://server/share` addresses, the common
`smb//server/share` typo, and Windows-style `\\server\share` paths are supported.
For SMB servers, typing only `smb://server` opens a share picker. Invalid or
inaccessible folders show an error rather than silently navigating somewhere
else.

Vertical separators distinguish navigation, the path, and places shortcuts.
Address rows and panes have visible borders, with a contrasting resizable
divider between the two panes in either orientation.

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
  (SMB/CIFS, NFS, SSHFS and GVFS). This file chooser lists mounted locations
  only; mount an unmounted drive from the main sidebar first.

Click an entry to open that folder; hover it to see the full path.

## Sharing color themes

The bundled **User Manual** includes a Theme Editor guide. Its button is
available in the Theme Editor and its background and colour tools.

Custom color themes are saved in the user data directory:
`$XDG_DATA_HOME/swordfish/themes/colors/` (or
`portable-data/themes/colors/` in a portable run). Each theme has its own
folder containing `theme.swordfish-theme` and any textures, patterns or
background images it uses. Imported themes and graphics remain available
after restarting Swordfish. Unused saved themes keep only their list details
in memory; their full settings are loaded when needed.

In Preferences → Appearance, use **Export theme** and choose a parent
directory. Swordfish creates a shareable theme-named folder containing
`theme.swordfish-theme` and its referenced `textures/`, `patterns/` and
`images/` assets. Import that file on another machine with **Import theme**;
keep the adjacent graphics folders with it.
Alternatively, copy the complete theme folder into `portable-data/themes/colors/`.
Each theme's own graphics take precedence over same-named shared or bundled
graphics. Swordfish resolves their absolute paths at runtime, so the folder
can be moved between machines. Legacy custom textures in
`~/.config/swordfish/textures/` remain readable.
Texture PNGs can also be organized into category subfolders under
`portable-data/textures/`; the editor can filter categories and copy new
textures into them. Textures in the folder root are uncategorized.
PNG patterns use the separate `portable-data/patterns/` folder and the
same category rules. Exported themes include referenced pattern and texture
files in their respective subfolders. Additional bundled texture and pattern
samples are adapted from public-domain Poly Haven assets; their sources,
authors and licence are listed in `share/swordfish/cc0-theme-assets.md`.
Background PNG and JPEG images can be imported into categorized subfolders of
`portable-data/images/`; choose **Background image** for the file list, tree,
Places, tabs or toolbars, then choose Fit, Fill or Tile. Apply changes only the
editor mockup; Save activates them. Images may be at most 32 MB, 8192 pixels
per dimension and 32 megapixels.
