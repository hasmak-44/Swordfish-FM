# Swordfish FM: project history and recovery guide

Last updated: 23 October 2026, after commit `7103fe4` and the RC6 Test 1
build (see sections 3.14 and 9.0). Earlier sections were written after RC4.

**Purpose:** preserve enough history, decisions, architecture and operational
knowledge to resume development if the chat, development machine or temporary
test artifacts are lost. Read this before starting another stage.

This is a reconstructed history, not a verbatim transcript. It uses repository
commits, release notes, saved conversation checkpoints and user test reports.
The earliest recoverable Swordfish commits here date to 2 October 2026.
Some experiments happened between commits, so their exact dates are not asserted.
Historical implementation details below do not override the current code.

## 1. Start here if the conversation is lost

1. Read this file, [README.md](README.md), [RELEASE-NOTES.txt](RELEASE-NOTES.txt),
   and [portable/README.md](portable/README.md).
2. Inspect the current branch, commit and both staged and unstaged changes.
   Do not reset or clean the worktree to make it look like a fresh checkout.
3. The frozen application baseline is **0.0.1 RC4**:
   - Repository: <https://github.com/hasmak-44/Swordfish-FM>
   - Development/default branch used for this work: `portable-layout`
   - Release tag: `swordfish-portable-0.0.1-rc4`
   - Application commit: `ff33180f0321c7d1192e579973d7f4d88014da7a`
   - [Public RC4 prerelease](https://github.com/hasmak-44/Swordfish-FM/releases/tag/swordfish-portable-0.0.1-rc4)
4. Treat RC4 as locked. Make later changes in later commits; do not move its tag,
   silently replace its assets, or imply that untested development is RC4.
5. The **Network discovery section is intentionally hidden**, not accidentally
   missing. Mounted shares and Connect to Server remain available.
6. The native remote loader is a major verified improvement. Do not remove
   `swordfish-network` just because discovery is hidden: its `--list` mode
   provides the fast mounted-share loader.
7. The latest user acceptance sequence was: fast remote navigation and triangle
   expansion, archive-folder open/close, then over five minutes idle without
   the previous timeout warning.
8. Before claiming a feature is implemented, distinguish code, automated
   validation, portable validation and the user's real-machine confirmation.
9. For the latest state (RC5, the RC6 cycle, the RC6 bug list and what to do
   next), read sections 3.14, 3.15 and 9.0 first. RC5 superseded RC4 as the latest
   public release; both stay frozen.

This guide is a documentation addition after the frozen RC4 application commit.
It is not part of the already-published RC4 source archive.

## 2. Project identity and working principles

Swordfish FM is a fork of Roland Baudin's Xfe/X11 file manager, with X Win
Commander ancestry. It is a native C++ application using FOX 1.6 and X11,
not a web application. Preserve upstream copyright, attribution and GPLv2
heritage; [COPYING](COPYING) is the license reference.

The project aims for a fast, capable, customizable file manager that works
across desktop environments. The user prefers function over decoration,
careful diagnosis and incremental changes, with real testing between stages.

Important established direction:

- Two independently usable file panes plus a shared sidebar/tree, not a
  literal four-pane Q-Dir clone.
- Each pane has its own tabs, navigation and footer/status area.
- Separate address bars by default; an optional common address bar follows
  the active pane.
- Keep portable operation working throughout development.
- Prefer small repo-native C++ changes over framework rewrites.
- Do not replace working behavior based only on visual resemblance to another
  file manager.
- External desktop applications are the desired direction for content handling.
  Complete removal of the legacy companion applications has **not** happened.
- Long-term Wayland/cross-platform ambitions were discussed, but no such port
  has been delivered and Rust is not a requirement.
- Preserve the approved Open With design and user settings.
- The user has often requested discussion before a new test build. Confirm
  scope when requirements or release choices are ambiguous.
- Build-retention preferences changed with disk pressure: first retain
  comparison builds, later keep only the latest local bundle when necessary.
  Do not apply either instruction as blanket permission to delete artifacts.

## 3. Chronological history

### 3.1 Initial rebranding and recovery: 2-6 October

Early commits rebranded Xfe to Swordfish FM, changed application/config/help
names and desktop entries, added the logo/banner and credits, and introduced
the 0.0.1 version. Later icon/install experiments included a 0.0.2-labeled
attempt. That label is historical; it is not the current release progression.

The user reported breakage affecting both Xfe and Swordfish after earlier
changes. Development returned to a known working 0.0.1 baseline, associated
with the recovery branch `swordfish-0.0.1-fixed`. The portable work then moved
to `portable-layout`, which became the working/default branch.

Key surviving commits:

| Commit | Milestone |
|---|---|
| `bd3ed32` | Initial Xfe-to-Swordfish rebrand |
| `9a61c7f` | Version set to 0.0.1 |
| `aa8792b` | Restored fixed Swordfish 0.0.1 baseline |
| `3110b77` | Added isolated portable Linux bundle |

The reset/recovery predates the detailed retained chat checkpoints. Do not
invent a complete account of the failed experiments.

### 3.2 Portable distribution, small-screen UI and downloads

The portable package became a relocatable AppDir-style folder launched through
`AppRun`, with sidecar user data rather than a system-wide installation.

Early portability/UI work included:

- Scrollable Preferences and About text for small screens.
- Better dialog sizing while preserving full message-box text.
- Cleanup of user-visible Xfe branding without removing upstream credits.
- Correct `swordfish` launches for new windows/root windows.
- A GitHub Actions portable build workflow.
- Fixing output permissions by creating the host bind-mount directory before
  a non-root container writes into it.

The user wanted a normal browser download, not an installer requiring GitHub
CLI, Python/Tk or additional download tools. A CLI installer and a Python
download helper were tried, then superseded by browser instructions.

GitHub Pages was configured to publish a single portable ZIP rather than
uploading the entire AppDir tree as Pages content. The user confirmed the
download worked. Later numbered releases became the durable distribution
reference. A mutable Pages URL must not be assumed to identify a particular RC.

Representative commits: `103bede`, `ef468e6`, `6cf61fe`, `db5f290`, `8a5196a`.

### 3.3 Branding icons and Deepin portability

The user selected two branding masters:

- Dark-blue folder for launcher/general application branding.
- Blue circle for running-window, dock/panel/tray-style identification.

Masters are in [icons/branding/masters/](icons/branding/masters/). Raster
sizes from 16 through 1024 pixels and scalable hicolor entries were generated.
The scalable assets embed raster artwork; they are not newly drawn vectors.

FOX big/mini icons alone did not consistently identify running windows.
Investigation included `WM_CLASS`, `WM_HINTS` and `_NET_WM_ICON`. Publishing
the circular `_NET_WM_ICON` helped resolve the dynamic task/window-button
icon issue. The user confirmed the circle in the relevant MX panel modes.
Do not rename internal FOX identity merely to change an icon; that can affect
settings and window matching.

Deepin testing exposed:

1. Unquoted launcher exports failed when the extracted path contained spaces.
2. Directly running a bundled binary bypassed its environment setup and could
   fail on missing OpenGL libraries.

The launcher was quoted correctly. Generic GLVND dispatch libraries
`libOpenGL.so.0` and `libGLdispatch.so.0` were bundled, while host graphics
drivers remained external. Portable path-with-spaces checks passed.

Key commits: `c7d72a0`, `18f5748`, `715f57a`.

### 3.4 Root launches, application routing and persistent logs

Portable root launches initially authenticated but did not show a window.
The command was bypassing `AppRun`, and PolicyKit removed X display
authorization. Routing through `AppRun` and forwarding the needed X11
display/authorization variables fixed the tested pkexec path. That did not
prove every sudo or administrator path on every desktop.

Legacy executable names and companion-app defaults also caused failures.
Swordfish program names and Xfe compatibility aliases were added.

Application-opening behavior went through several experiments:

- Global system applications versus internal applications.
- A later three-way system/internal/custom preference.
- Investigation of apparently missing text associations.
- A registry fix to load bundled defaults before user overrides.
- Eventual movement toward system defaults plus explicit Swordfish-local
  overrides rather than global mode rewriting.

These stages are history, not instructions to reintroduce obsolete modes.

Persistent categorized logging replaced unreliable ad-hoc diagnostics.
Categories are application, UI, file operations, settings and errors.
The intended detail level is semantic actions, paths and outcomes, not raw
keystrokes, passwords or pointer movement. Logging is explicitly instrumented;
it is not automatic coverage of every action.

### 3.5 System icon themes, language and color-theme sharing

System icon support maps freedesktop theme names to Swordfish's internal icon
names using [icons/system-icon-map.txt](icons/system-icon-map.txt).
SVG icons are converted using bundled `rsvg-convert`, cached once per selected
theme, and displayed through the existing FOX image path.

An initially blank conversion dialog was replaced with a message, progress
bar and estimated time remaining. Coverage varied by theme; early tests found
strong coverage for Papirus and several other themes, not universal completeness.

The Language setting was added with a system-default choice and available
translations named in their own languages. A restart bug retained the previous
`LANGUAGE` value after choosing System default. Remembering/restoring the
original environment fixed that sequence.

Custom color themes became importable/exportable as `*.swordfish-theme`,
containing a name, tip and 12 color values. Imports validate fields and handle
name clashes. Upstream copyright/help attributions were retained.

Commit `4778969` records the icon/language/theme-sharing milestone.

### 3.6 Archive folders, FUSE refresh and context commands

The user supplied working archive-folder scripts using host `archivemount`.
Some apparent copy/move delays were actually stale file lists: FUSE mounts
did not change parent directory modification times as the scanner expected.

Changes included:

- Temporary boosted refresh after scripts/mount operations.
- More frequent FUSE rescans.
- Selecting/revealing newly created entries.
- Archive-folder icons and type labels.
- Built-in Open as Folder / Close Archive Folder context actions.
- Host command lookup/environment handling for `archivemount` and unmounting.
- Refusing to mount over a nonempty folder and using read-only mode when needed.
- Moving panes/tree out of an archive before unmount/save.

The user confirmed the archive-folder workflow. Commit `359eb4d` records
this stage. Archive editing remains dependent on host support and archive
format capabilities; do not promise every format is writable.

A child-signal bug in the mount-command wrapper was discovered and fixed much
later in Test5; see section 4.

### 3.7 Independent panes, expandable Details folders and bookmarks

Q-Dir screenshots informed discussion, but the user explicitly chose two panes
plus a tree. Each pane was treated as an independent asset.

Implemented:

- Expandable folders in Details view, indentation and hierarchical sorting.
- Right/Left keyboard expansion, directory symlinks and nested operation paths.
- Expansion memory across refresh and revisiting a directory.
- Correct containing-folder targets for drops on nested file rows.
- Cleanup of a definite drag/drop busy-cursor leak.
- Per-pane Back/history/Forward/Up, Home/Root/Desktop and address controls.
- Breadcrumb/editable-path toggle, Ctrl+L, Enter and Escape behavior.
- Per-pane scrolling tab bars, close/reorder controls and visible `+`.
- Blank tab-bar space actions and active-pane routing of global commands.
- Independent saved/restored left/right tabs, with migration of older shared tabs.
- Address -> tabs -> file list -> footer ordering.
- Optional common address bar, mutually exclusive with separate pane bars.
- Pencil placement to the left of the address, grouping separators and clearer
  pane/address borders and splitters.
- Left/Top and Right/Bottom menu terminology.
- Theme-derived bar shading.
- Bookmark management and named tab groups.
- Preserving pane/tab/active-state across preference restarts.

Early arrow logic probed every child for emptiness. That was later recognized
as a serious remote-performance problem and replaced with lazy detection.

### 3.8 Open With, file chooser and manual remote navigation

Open With first gained installed desktop applications alongside recent custom
commands. Shared desktop-entry handling covers localized names, command
placeholders, host XDG paths and filtering of unsuitable entries.

The browse dialog gained Recent, Places and Devices, including common
directories, bookmarks, recent host desktop locations and mounted storage.

Manual remote navigation/Connect to Server work is distinct from discovery.
SMB URI/share-picker workflows were developed using host GIO/GVFS. Already
mounted shares were visible under Devices even while server discovery failed.
Do not equate "Network discovery does not work" with "SMB browsing is absent."

The approved later Open With design uses a searchable grouped application
list, icons, custom command, keyboard filtering and an unchecked-by-default
option to remember a Swordfish-local extension association.

### 3.9 RC1, executable regressions and RC2

RC1 was published at `b041ce1`, then superseded after executable/application
and terminal regressions. Numerous `executable-fixN` bundles were intermediate
tests, not separate stable releases.

Important lessons and fixes:

- Executable programs must not be treated as ordinary text documents.
- Scripts must respect their shebang/interpreter rather than forcing Bash
  scripts through `sh`.
- No automatic chmod or automatic elevation after permission errors.
- Runnable JAR handling was tested with a standalone installer.
- A tinyMediaManager JAR failed outside Swordfish too; its supplied Bash launcher
  was required. This was not proof of a generic JAR-launching defect.
- Wine/MSI and managed Mono handling were considered in the shared classifier.
- Host applications need host libraries/XDG settings, while bundled tools must
  remain available where appropriate.
- Old-glibc containers must never link copied host object files.
- Terminal actions need the actual current directory and terminal-specific
  command argument syntax, not just a generic `-e`.
- Desktop-default terminal selection and explicit local overrides were added.
- Application errors and missing saved apps must remain visible.
- Settings Cancel must not commit pending choices.
- MIME discovery must not interpret absent bundled MIME metadata as proof
  that no desktop application is configured.

RC2 was committed as `40d7a37` and published after explicit user approval of
the tested intermediate implementation. The earlier roadmap contained later
agreements that were not all included in RC2. RC1 binary publication was
withdrawn during that process; its historical source tag remained.

### 3.10 RC3 and the corrected sidebar/view redesign

Commit `3889182` published RC3 with expanded-folder sorting corrections,
editable-address/tab preservation, Open With improvements and initial sidebar,
preview and discovery work.

Dragging from GTK archive managers initially failed because they used
`XdndDirectSave0`, not ordinary URI drops. Direct Save support was added to
file-list and tree targets, with destination/name validation. The user
confirmed File Roller and Engrampa drag-out worked.

Preferences had taken 45-60 seconds on the user's machine. Repeated per-MIME
subprocess queries were replaced with direct host XDG defaults parsing and
installed-app lookup. Focused local timing became milliseconds, which is not
the same as a full real-machine benchmark.

Slate Blue became the fresh-install fallback, preserving saved colors.

RC3's initial sidebar interpretation was wrong: the user wanted one ordinary
hierarchical Places tree, not stacked independently scrolling sections.
The corrected design retained Places and Tree tabs, used hidden existing
models for persistence/device polling, and added nested folder expansion,
shared scrollbars and remembered root collapse state.

Previews were corrected to use the actual selected pathname and became a
dismissible bottom overlay on either sidebar tab, with scrolling clearance.
Bounded text and regular-file/selection guards were included.

The final view agreement superseded big/small-icon and fourth-view proposals:

- **Details, Icons, Thumbnails** are three distinct per-tab views.
- Zoom is shared across both panes and all tabs, with bounded steps/reset.
- Zoom does not scale UI fonts or the sidebar tree.
- Icons use compact row-major cells without decorative unselected tiles.
- Thumbnails use equal cells, subtle shading and thin borders, proportional
  image fitting, folders first and ellipsized names/full-name tooltips.
- Files without usable previews are omitted from Thumbnails, not shown as
  placeholder tiles. Other views still show ordinary files.

### 3.11 Preview engines and host GIO isolation

The user approved images, SVG, PDF and video stills despite the larger bundle.
A separate asynchronous `swordfish-preview` process now generates bounded PNGs:

| Content | Engine |
|---|---|
| Raster images | GdkPixbuf |
| WebP | libwebp |
| SVG | librsvg |
| First PDF page | Poppler GLib/Cairo |
| Video still | ffmpegthumbnailer/FFmpeg |

FOX remains the GUI and displays generated PNGs; it is not the source decoder.
The queue/cache supports priority, invalidation and resource/time limits.
These limits are reliability controls, not an OS security sandbox.

Loading modern host GVFS modules into bundled older GIO caused ABI problems.
The portable GUI uses an isolated module directory; host commands restore
the host environment; the preview worker uses local VFS.

Discovery also gained a dedicated host-GIO helper. On an unmounted SMB browsing
root it attempts anonymous browsing-root activation and one retry, not mounting
user shares or collecting passwords. Lifecycle tests passed, but the user still
could not discover their network. Full discovery was not established.

### 3.12 Remote performance investigation and the Test1-Test5 sequence

The critical user report was a long freeze opening an already mounted SMB share,
sometimes apparently affecting the desktop/taskbar/dock. Details expansion
was suspected.

1. **Share Freeze Test1:** removed eager child-directory opening merely to draw
   expansion arrows. Released/flushed pointer grabs before slow navigation and
   expansion. This improved behavior but did not solve loading speed.
2. **Large Folder Test2:** reused metadata, avoided repeated stat calls and
   started busy feedback earlier. Local latency simulation improved, but the
   user's 793-folder directory still took about 19.3 seconds.
3. **Expansion Diagnostics Test3:** instrumented actual metadata/access timings,
   cached user/group names within listings, and covered more triangle cursor
   paths. Real logs showed roughly 20 seconds with about 9.7 seconds in lstat
   and 9.6 seconds in access, then about 10 seconds on later metadata-only scans.
4. Further user measurements were 31.84 seconds for expansion and 30.61 seconds
   for double-click, with busy feedback ending too early. Caja took 2.32 seconds
   for the comparison procedure.
5. The user supplied Caja source. Its async GIO enumeration, batches of 100,
   returned metadata, cancellation and queued UI updates informed the design.
   Swordfish did not import GTK/Caja architecture wholesale.
6. **Native Remote Test4:** added a host-GIO asynchronous helper, shared snapshots,
   progressive batches, cancellation and main-thread FOX updates across Details,
   Places and Tree. The user's first 31.14-second report was corrected: they
   had accidentally launched the old build. The actual new build appeared to
   load in under three seconds, then passed expansion and other-location tests.
7. User logs subsequently recorded the 793-entry native listing at **929 ms**,
   with later refreshes **219-358 ms**. These are logged loader times, not a
   claim of exact end-to-end UI timing or a controlled Caja benchmark.
8. Test4 later displayed idle timeout dialogs after archive-folder use.
   The separate lifecycle defect is explained next.
9. **Native Remote Test5:** fixed that lifecycle defect. The user confirmed
   fast browsing, archive open/close and over five minutes idle without warnings.

### 3.13 Post-RC4 SMB discovery research and diagnostic: 18 October

This work is after RC4 and does not change the frozen application. The rule
agreed with the user was to reuse proven approaches, cover Linux and **all
Windows versions**, solve discovery before mounting, and never mount
automatically, enable SMB1 or weaken authentication.

1. **Source study** of Caja, Thunar, Pnethood (Puppy) and Smb4K, both the
   installed 3.1.7 and a 4.0.80 development snapshot:
   - Caja and Thunar browse GVFS `network:///` and follow
     `standard::target-uri`. Swordfish's old helper enumerated only `smb://`.
   - Pnethood uses `nbtscan`, `smbclient -L` and `mount.cifs`.
   - Smb4K uses DNS-SD, optional WS-Discovery with a WS-Transfer metadata Get,
     and `mount.cifs` through a KAuth helper.
   - Smb4K allows a **named user with an explicitly empty password**, and uses
     guest only when the username is empty. The user's Windows 7 machines rely
     on this, so a blank password must not be treated as guest. This is
     relevant later, for mounting.
2. **Standalone diagnostic** `portable/network-discovery-diagnostic.py`, with
   offline tests in `tests/network-discovery-diagnostic-test.py`:
   - It runs three methods concurrently: host GVFS (`network:///`,
     `dns-sd://local/`, `smb:///`), IPv4 WS-Discovery and NetBIOS
     (`nmblookup`).
   - Follow-up checks: a WSD metadata Get on the replying address only, and a
     TCP-connect-only check of port 445.
   - Usage and safety limits are documented in
     [portable/README.md](portable/README.md).
3. **Live results** on the user's LAN (MX-RYZEN, 192.168.1.106, wlan0):

   | Machine | GVFS (Caja/Thunar) | NetBIOS | WS-Discovery | TCP 445 |
   |---|---|---|---|---|
   | MX-HP (Linux, 192.168.1.102) | yes | yes | no | open |
   | MX-RYZEN (this computer) | yes | yes | no | open |
   | DELL-740 (Windows 7, 192.168.1.104) | **no** | **no** | **yes** | open |

   - Windows 7 answered WS-Discovery in 2.06 s, on an old machine over a slow
     network, without needing the retry.
   - Its probe reply had no name. The metadata Get (0.24 s) returned
     `DELL-740/Workgroup:MAKHLOUF`.
   - This explains why Caja and Thunar miss Windows machines while Smb4K and
     Pnethood find them.
   - Windows 10/11 has **not** been tested yet.
4. A fixed bug: the GVFS DNS-SD scheme is `dns-sd`, not `dnssd`.

### 3.14 RC5, tester reports and the RC6 cycle: 18-23 October

**RC5** (`swordfish-portable-0.0.1-rc5`, commit `0dc2a35`, 18 October) shipped
the SMB share browsing and mount/unmount lifecycle described in section 9.
It still embedded version `0.0.1-rc4` because `configure.ac` was not bumped.

On 23 October a photographer tester's report arrived (tab drag to Bookmarks,
mount buttons, About version/links, duplicate shares, false `udisksctl`
error, stale device rows, long network labels, thumbnail sort headers, tabs
not restored, and folders listing only some of their files).

- **Incomplete listings:** fault injection showed that silent `lstat`
  failures drop entries and a `readdir` EIO yields a partial list without
  an error. The tester's folder could not be reproduced. A GitHub
  prerelease `swordfish-rc5-listing-diagnostic-1` (TEST ONLY; its
  `LISTING-DIAGNOSTIC.md` instructions ship in that archive) logs
  every entry when `SWORDFISH_LISTING_DIAGNOSTIC=1`. **Closed on
  25 October as a false alarm:** the tester had compared two folders with
  similar names but different contents. No listing bug was found. The
  trace code was later committed behind the log level setting (section 9.0).
- **Tabs not restored** (`bb7a3d4`): accepting Preferences re-read the
  registry before saving custom themes, discarding the dialog's changes, so
  users with custom themes could never enable "Restore tabs". `saveConfig`
  now re-reads `xferc`, not the `swordfishrc` defaults copy.

The user then gave a to-do list, grouped as: 1 tabs/bookmarks, 2 visibility,
3 network names, 4 file list, 5 settings/themes, 6 About/version,
7 big features. Commits on `portable-layout` (all by hasmak-44 with a
Copilot co-author trailer):

| Commit | Change |
|---|---|
| `4704b38` | Tab drops on sidebar Bookmarks from either pane and onto an empty folder; tab menu "Add to Bookmarks" / "Bookmark All Tabs as Group"; groups shown as "name (group)" with Open/Rename/Remove; drag tab onto tab to reorder; active tab tint and minimum width; hover dragged files over a tab for 2 s to switch, drop on a tab to copy/move/link. |
| `4011f35` | Accent strip on the active pane, bordered sidebar without the "Folders" row, sidebar highlights the active tab's folder (or nearest visible parent; `smb://` highlights its host). |
| `7671157` | Hide `ADMIN$`, `IPC$`, `PRINT$`, drive `C$` shares and Samba "nobody"; remember discovered computer names (`[NETWORK NAMES]`) and use them in tabs, labels, address bar and title. |
| `8649e77` | Version `0.0.1-rc6`; Swordfish FM copyright beside the retained Xfe notice; regenerated autotools output. |
| `dcb4650` | Type-to-find also matches rows inside expanded subfolders (it compared the stored `tv/Show` label). |
| `d913800` | Background folder sizes and item counts in Details; "Folder Sizes" panel submenu; status bar "X free of Y", selected folder size/counts, multi-selection totals. |
| `d03ea17` | Size on disk (allocated blocks, hard links once, other filesystems skipped); sorting by Size sorts folders by size or count depending on the display mode (still-counting folders last); mode also in Preferences > File List. |
| `05d8d80` | Panel path editor sometimes did not appear (path row not marked for layout); most visible in the top/left panel. |
| `5ea3bca` | Inline rename: F2, Rename menu, or slow second click on the only selected name; extension left unselected; Enter/click away renames, Escape cancels; multiple selections still use the dialog. |
| `d85ad73` | Icons view shows image previews while still listing every file (Preferences > File List > Previews option); Thumbnails view shows only previewable files and the status bar says how many are hidden; LibRaw extracts embedded previews of CR3, CR2, NEF, ARW, RAF, ORF, RW2, PEF, DNG, SRW with camera rotation; visible items jump the preview queue. The portable build compiles LibRaw 0.21.5 from a pinned tarball. |
| `7103fe4` | Thumbnail loading no longer jumps: full list and scrollbar appear at once, previews replace icons in place (visible first) instead of a full rescan every second, no scroll-to-top; pending files stay listed and are dropped from Thumbnails view only if their preview fails. |

Decisions recorded with the user:

- Folder size display options are None / Item count / Size on disk / Both.
  Sorting by size uses size whenever size is shown, otherwise item count.
- Thumbnail placeholders are deferred: the photographer tester warned that
  hiding RAW or sidecar files is risky (users move contents and delete the
  folder). Icons-view previews were chosen instead, keeping every file visible.
- When the user attaches the `AUTHORS` file with a message, ignore it.
- The user prefers short replies and plain-text questions (pop-up question
  forms cover their view).

### 3.15 Log level, RC6 bug list and the remount fix: 25 October

- `e72535f` recorded the listing false alarm. `2104a96` added the log level
  setting (section 9.0). Both are pushed. The exact committed tree builds with
  `./autogen.sh && ./configure && make`; building with the checked-in
  `src/Makefile.in` fails to link (`Preview::shutdown`) because that file is
  stale. This predates the log level work.
- The `swordfish-rc5-listing-diagnostic-1` prerelease and its tag were
  deleted at the user's request. The obsolete uncommitted
  `portable/LISTING-DIAGNOSTIC.md`, `portable/run-listing-diagnostic`, their
  two packaging lines and the README link were deleted on 25 October.
- Codespace disk was 91% full. Removed 9 old Copilot CLI versions from
  `~/.cache/copilot/pkg/linux-x64` (kept the 2 newest), the plain Debian
  Docker images (kept `swordfish-portable-builder`) and the cpptools cache;
  now about 85%. Memory is also tight (8 GB): several stale VS Code extension
  hosts stay alive after reconnects, so `make -j$(nproc)` gets killed. Use
  `make -j1` in scratch builds, or restart the codespace.
- The original long chat (session `1c1cc868…`, 38 checkpoints, about 690 MB
  of events) was too large to restore and was deleted on 25 October, after the
  user downloaded `Swordfish-before-detachment-backup.tar.gz` (the pre-detachment
  repository backup; sha256 `770aba97…16611`) and confirmed it extracts. Its 38
  checkpoint notes were kept in the current session folder:
  `~/.copilot/session-state/e0acc086-d9c4-4a30-a2ec-10767b8e7db5/files/old-chat-1c1cc868-checkpoints/`.

**RC6 bug list from the user** (`Bugs.txt`, 25 October). Agreed plan: fix
them in a bundle and build one test package at the end.

| # | Area | Report | Status |
|---|---|---|---|
| A1 | Devices | Mount/unmount buttons do nothing | **fixed** |
| A2 | Devices | Shares mounted twice or remounted at random | parked: not reproducible |
| A3 | Devices | Drive unmounted elsewhere stays in the sidebar until restart | **fixed** |
| A4 | Devices | Unmount fails: udisks `DeviceBusy`, "target is busy" | **fixed** |
| A5 | Devices | After unmounting in Swordfish and remounting in Thunar, the drive is listed but "Folder ... doesn't exist" until restart | **fixed** |
| B1 | Network | Windows server without a password not accessible (check how smb4k does it: guest first) | **fixed** |
| B2 | Network | Connect to Server button, Shift+F5 and File menu do nothing; reuse the discovery/address-bar method | **fixed** |
| B3 | Network | Sidebar and title bar show full paths; want `share@server` (tab label already done) | **fixed** |
| C1 | About/Help | About shows the wrong version; links in About/Help body text are not clickable (incl. GitHub Issues) | **fixed** |
| D1 | File list | Inline rename and panel-to-panel drag erratic (selection lost?) | **fixed** |
| D2 | File list | No auto-scroll when dragging to an item out of view | **fixed** |
| D3 | Tabs | Drag-hover tab switch: make it 1.5 s; the switched tab opens empty for about 2 s | **fixed** |
| E1 | Settings | Remove the thumbnail size setting (replaced by zoom) unless it still does something | **fixed** |
| E2 | Address bar | Right-click menu while editing lacks Copy/Paste | **fixed** |

User's scenario for the device bugs (USB HDD "My Passport 4", `/dev/sdc1`,
ntfs3, automounted by Thunar at `/media/Hassanein/My Passport 4`):

- Unmounted in **Thunar**: the drive disappears from both programs. Normal
  (Thunar also powers off/ejects it).
- Unmounted in **Swordfish**: it leaves the Swordfish list but stays greyed
  out in Thunar. Swordfish runs plain `umount` (`PROGS/unmount`, default
  `DEFAULT_UMOUNTCMD`), which bypasses udisks. After remounting from Thunar
  (same folder name), Swordfish listed it but said it doesn't exist until
  restarted.

**A5 cause and fix:** `xf_stat`/`xf_lstat` in `src/xfeutils.h` return -1
without calling `stat` for a mount point marked "down" in the global
`updevices`. When the folder vanished during the 5-second `mtdevices` window,
a check marked it "down", and `updevices` is only rebuilt every 300 s. The fix
drops the cached state when a mount point appears or disappears in
`DirList::onMtdevicesRefresh` and `FileList::refreshMtdevices`, and after any
mount/unmount command in `File::mount`. Changed: `src/DirList.cpp`,
`src/FileList.cpp`, `src/File.cpp`. Compiled in a scratch build; not yet
tested on the user's machine.

**Planned next for the device group:** unmount `/media` and `/run/media`
drives with `udisksctl unmount -b <device>` (and offer Eject / power off),
so Thunar stays in sync; move tabs and panels inside the drive to Home before
unmounting (avoids "target is busy"); then A1-A3.

**RC6 bug bundle (committed together, 25 October).** All items above are
fixed except A2 (parked: the user could not reproduce it).

- **Devices (A):** `/media` and `/run/media` drives unmount through
  `udisksctl` (Eject only for hotplug drives), panels inside a drive move to
  Home first, a busy message names the cause, the Mount/Unmount buttons work,
  and the sidebar drops drives unmounted elsewhere (`xf_hashfile` fix, see
  `tests/folder-expansion-regression.cpp`).
- **Settings/address bar (E, extra):** thumbnail size setting removed (zoom
  replaces it); right-click menu on the path editor (Cut/Copy/Paste, Paste and
  Go, Select All). The "common address bar" option and its code were removed
  at the user's request; Ctrl+L opens the panel path editor. The freed space
  is reserved for a future custom toolbar.
- **About/Help (C):** body links are clickable (`LinkText` in
  `MessageBox.cpp`) and readable on dark themes.
- **Tabs (D3):** drag-hover switch after 1.5 s, the list is redrawn at once.
- **File list (D1, D2):** slow-click rename no longer depends on list focus
  or `current`; it arms anywhere in the Name cell (list/mini-icon modes) and
  only in the active panel; `FileList::setDirectory` no longer resets the
  current item when the folder is unchanged. Drop targets restore the hovered
  folder's selection. A drop-scroll timer (`ID_DROP_SCROLL_TIMER`) scrolls the
  list while dragging near its top/bottom edge.
- **Network (B):** guest access tries anonymous, then user `guest` with an
  empty password (smb4k style) in both `resolveNetworkPath` and the share-list
  helper (`NetworkDiscovery.cpp`), before asking. Connect to Server was
  simplified at the user's request to Type (SMB, SFTP, FTP, FTPS, WebDAV,
  WebDAV HTTPS), Server, Port (0 = standard) and an optional share/folder; it
  builds the address and hands it to `FilePanel::resolveNetworkPath`, so the
  server asks for the login. SMB without a share opens the server tab. The
  login prompt leaves Domain blank; `WORKGROUP` is only sent internally.
  Window title and sidebar show `share@computer` / `user@host`
  (`RemoteDirectory::mountLabel`, `titlePath`).
- Not tested against a real server (no SMB/SSH in the codespace).
- Bundle committed as `5f0949d`. **RC6 Test 2** package
  (`Swordfish-RC6-Test2-Portable.tar.gz`, sha256 `f9631d43…49951`) was built
  with `./portable/build-portable.sh` from a clean `git archive` of
  `5f0949d`; it runs and reports `0.0.1-rc6`. It was **not** published on
  GitHub: the user downloaded it through a temporary codespace port link,
  which was then stopped and the copy deleted. Rebuild from `5f0949d` if needed.
- Clean-up the same night: deleted the uncommitted listing-diagnostic files
  (`b084062`); deleted the local RC5 release files from `build/` (all four are
  on the RC5 GitHub release) and `build/discovery-diagnostic`, keeping the
  user's notes `build/road map.txt` and
  `build/Swordfish-LinuxQuestions-announcement.txt`. Committed `AGENTS.md`,
  `.github/copilot-instructions.md` and the portable README device section
  (`96f0c80`). Still uncommitted on purpose: `.vscode/`, `install-swordfish.sh`
  (unreviewed), the downloaded repo zips and generated autotools/po files.

### 3.16 RC6 Test 2 feedback and network mount reuse: 26 October

The user downloaded and extracted the Test 2 portable bundle, then supplied
an updated `Bugs.txt` through MEGA on 26 October. The prior "fixed" statuses
describe implementation, not successful validation on the user's machine.
The new report reopens the following items:

- Mount is disabled and Unmount is unavailable for some mounts.
- SMB shares sometimes mount twice after entering a password; cancelling
  the prompt can leave a single usable connection. Guest versus cached
  account identity is unknown. Guest was configured read-only in Windows,
  but the user could edit and save a file. Do not infer authenticated
  identity from the displayed Unix permission bits.
- Unmounted internal partitions and USB drives should remain visible,
  greyed and remountable; Eject is missing for removable devices.
- Reduce tab drag-hover switching from 1.5 seconds to 1 second.
- Left tab bar disappeared once in vertical layout; restart fixed it and
  it has not recurred. Screenshots mentioned in the report were not included.

The user authorized investigating the duplicate-mount/login flow first.
The following changes form the RC6 Test 3 source bundle:

- `FilePanel::networkLocalPath` checks the actual SMB FUSE mount folders
  under `$XDG_RUNTIME_DIR/gvfs` (fallback `/run/user/UID/gvfs`), decoding
  attributes independently of their order. An unspecified login reuses an
  existing mount; explicit user/domain values must match. `gio info` fallback
  uses `LC_ALL=C` because its `local path:` line is otherwise localized.
- Recheck after guest attempts and before submitting credentials, including
  after a login dialog closes, to avoid a second mount when a connection
  has already appeared. Existing-mount notification explicitly mentions
  possible saved credentials; logs record the reused path without passwords.
- Tests simulate a successful mount invisible to `gio info` under the
  original login URI, a cached authenticated mount, and a different explicit
  user. They check returned paths and exact mount command counts. Existing
  test expectations were updated for last night's short sidebar labels and
  the anonymous-plus-guest timeout sequence.
- Validation: clean scratch autotools build, then
  `tests/run-folder-expansion-regression.sh` with staged installed icons;
  complete suite passes. No real Windows/SMB server was available, so this
  is a tested client-side correction, not confirmation of Windows account
  identity or of every reported network issue.

**Device follow-up (26 October, Test 3):**

- Found the inventory parser's core fault: searching for `TYPE="` matched
  inside `FSTYPE="`, then returned an empty result instead of continuing to
  the actual `TYPE` field. Consequently all unmounted volumes were discarded.
  Fixed the field-boundary search and added realistic `lsblk --pairs`
  regression data.
- Devices now includes unmounted internal filesystems as well as hotplug
  volumes. Previously mounted desktop volumes retain their label even when
  unprivileged `lsblk` omits FSTYPE. Blank unknown volumes, swap, encrypted
  containers and filesystem members remain excluded. Inventory command
  failures are logged.
- Unmounted device rows render grey but remain enabled/clickable; they do
  not offer directory expansion. Disconnected volumes leave the list.
- Mount/Unmount toolbar target selection respects a focused Devices entry,
  ignores the parent (`..`) row, and resolves GVFS roots and containing data
  mounts. Unmount does not depend on a nonempty file list. Root is protected.
  Desktop mounts use udisks even when also listed in fstab.
- Eject's enablement also works with focused Devices entries and the legacy
  mount menu. Hotplug volumes qualify; internal drives and network shares do
  not. Busy unmount remains an error, not a forced detach.
- Validation: scratch build and full folder-expansion regression suite pass.
  New mocked host-command tests cover internal/USB inventory, missing FSTYPE,
  removal, Mount/Unmount/Eject enablement, and the command sequence for mount,
  busy failure, unmount, remount and safe eject. Physical hardware and desktop
  authorization prompts still require user testing.

**Tab follow-up (26 October, Test 3):**

- Changed `TAB_HOVER_DELAY` in `TabButtons.cpp` from 1500 to 1000 ms.
  The existing immediate refresh on timeout is preserved.
- Could not reproduce the one-off disappearing left tab bar. Added an
  explicit parent `recalc()` when `FilePanel::showTabbar` changes visibility,
  following the same FOX layout pattern used by the path editor. Redundant
  show/hide calls return without dirtying layout.
- Tests switch panel orientation and resize six times while hiding/showing
  both tab bars; assert nonzero tab/scroll/bar geometry and no vertical
  clipping. Direct hover-timeout dispatch verifies that the inactive tab is
  selected and its folder list is immediately populated.
- Scratch build and the complete regression suite pass. An attempted
  in-process real-DND test did not negotiate FOX's drag state and was not
  retained. The configured 1000 ms is verified in source, but end-to-end
  pointer-hover timing still needs desktop testing. The original intermittent
  missing bar is not claimed definitively resolved.

**Visible Devices action buttons (26 October, Test 3):**

- User clarified that a visible button is required in the device list, not
  only a context-menu command. Confirmed behaviour: the USB row button does
  Eject / Safely Remove; USB Unmount remains in toolbar and context menu.
- Sidebar rows now draw a right-aligned action button: Eject for supported
  hotplug devices (mounted or unmounted), Unmount for mounted internal
  partitions and network shares; no button for root or unmounted internal
  partitions. Tooltips name the action. Device classification is cached in
  the row during sidebar synchronization, not queried during painting.
- Press/release handling consumes only button clicks, releases the pointer
  before running disk operations, and cancels if released outside the button.
  Sidebar rebuilds are suspended while an action runs, like context menus.
- Scratch build and full regression suite pass, including a visible-button
  hit-test and simulated click through the actual safe-eject command path.
  Hardware behaviour still needs user validation.

Remaining: test network/device changes, the 1-second hover, and the tab-bar
layout safeguard on the user's machine. On 26 October the user authorized
saving and pushing this bundle, building RC6 Test 3 from its clean committed
source and supplying a private temporary codespace download link. This is a
test build, not a stable release. Record the build result and checksum below
after validation. Do not mark the entire bug report resolved.

**RC6 Test 3 delivery (26 October):**

**WITHDRAWN after user testing at 09:04 UTC on 26 October. Do not reuse or
redistribute this build.** The user agreed to discard it; the temporary
download server was stopped.

- Unmount remains active with no device selected and attempts to unmount
  Home. This is an unsafe target-selection regression; fix it before another
  test package. Do not infer a data mount from the current Home directory.
- All unmounted devices still disappear, including a local partition.
  Mocked inventory regressions did not establish behaviour on the user's
  host; investigate actual host inventory output before claiming this fixed.
- The user reported the system frozen after local-partition unmount. Cause
  is unknown; do not attribute it to authorization or a specific operation
  without evidence. Further testing of this build was stopped.
- Network mounting did not ask for a password, consistent with shared
  desktop GVFS session credentials, not proof of Guest identity.
- Drag-hover tab switching was reported acceptable.

**RC6 Test 4 delivery (26 October):**

- Source commit `91fa0999706280fd95a07be23d9fb3bf386a695c`, pushed to
  `portable-layout`; clean exported source built using
  `JOBS=1 ./portable/build-portable.sh` (Debian Buster, glibc 2.28).
- Filename `Swordfish-RC6-Test4-Portable.tar.gz`, about 76 MiB.
  SHA-256: `40bae52c4fee837b290854c0bd325a88e4d0f9c6c522e28517ea03b48e09acaf`.
- Archive listing and isolated packaged GUI startup pass; executable reports
  `0.0.1-rc6`. Regression suite passed before packaging, including real
  host inventory and command-level Root/Home refusal.
- Temporary private codespace download, not a stable release. Stop its
  server after download/extraction confirmation. Test 3 remains withdrawn.
- Limited test only: no-selection toolbar state in Home, grey unmounted
  device visibility, then spare USB mount/unmount. No internal-partition
  unmount tests; freeze cause still unknown.

**Post-withdrawal corrections (26 October, Test 4 source bundle):**

- Reproduced the inventory failure using the real host command:
  `lsblk -l -P ...` exits 1 with "mutually exclusive arguments".
  Removed `-l`; `--pairs` alone provides the needed records. The mocked
  command now rejects this invalid combination, and the regression suite
  calls the production `xf_unmountedvolumes` against real host `lsblk`
  read-only, in addition to the fixture-based lifecycle tests.
- Removed the toolbar's inferred current-folder/containing-mount target.
  Mount/Unmount now require an explicit focused Devices entry or one selected
  exact mount point; no selection, multiple selections and `..` do not
  select the current folder. No-device Mount chooser fallback removed.
  Arbitrary files, descendants and symlinks are not promoted to mount roots.
- Added shared protection for Root, Home and filesystems containing Home
  in toolbar eligibility/dispatch, sidebar actions, context unmount and
  desktop-drive operations, plus a final low-level `File::mount` refusal.
  The no-selection handler displays a warning and launches no command.
- Scratch build and full regression suite pass, including real inventory,
  no-selection dispatch, and low-level tests proving Root/Home refusal does
  not execute a supplied marker-writing command. Existing device/network/tab
  and visible-button regressions also pass.
- User authorized a limited Test 4 build at 09:18 UTC. Disappearing devices have a reproduced
  command-level cause and correction, but need host validation. Freeze
  remains unexplained; do not assert that correcting these faults proves
  the freeze resolved.
- Test 4 scope: first check that no selection disables Mount/Unmount while
  viewing Home, then check grey unmounted-device visibility, then use only a
  spare USB drive with no important data for mount/unmount tests. Do not test
  internal-partition unmounting while the freeze remains unexplained.
- Test 4 paused before testing (26 Oct, 09:35 UTC). User observation: clicking
  a discovered server always opened a new tab, so several tabs could point to
  the same share; after unmounting, leftover tabs may try to read the missing
  target. During the Test 3 freeze the pointer moved, but taskbar, dock and
  Start menu ignored clicks, and the system eventually recovered. This matches
  an X11 pointer grab still held while Swordfish was blocked on I/O (the
  ungrab request can sit unsent in the Xlib buffer). Hypothesis, not proven.
- Test 5 corrections: server click browses in the active tab (no new tab);
  every Unmount through `File::mount` (local and GVFS) closes matching
  active/background tabs in both panes and moves the tree before the command
  runs (UDisks eject already did); `File::mount` and `navigatePane` flush the
  X connection before blocking work. Local unmounts previously never closed
  tabs. Regression tests updated/added; full suite passes (36 PASS).
  Trade-off: a failed unmount leaves the user outside the mount.
- Test 5 built from clean archive of `900fd8d` (`JOBS=1`, Debian Buster,
  glibc 2.28): `Swordfish-RC6-Test5-Portable.tar.gz`, 79,674,848 bytes,
  SHA-256 `80c64a9cb4594bf531645473bf2d78681a62ce22548a78c6d0a94a30712368b6`.
  Reports `0.0.1-rc6`; isolated Xvfb startup passes. Test 4 superseded and
  its download stopped. Same limited test scope: no internal-partition unmount.
- Test 5 results (10:24 UTC): repeated server clicks reuse one tab (pass);
  share open in 3 tabs, all closed on unmount (pass). USB HDD Unmount/Eject
  refused: UDisks DeviceBusy "target is busy" for /dev/sdc1. No other app had
  the drive open; `fuser`/`lsof` empty afterwards; unmount worked after a
  while. Cause found: the preview queue (up to 128 jobs, `swordfish-preview`
  child per file, 12 s limit) kept opening files on the drive after its tabs
  closed. Fix: `Preview::cancel(mount)` drops queued jobs and kills a running
  child under the mount; called from `onNetworkUnmounted` before every unmount
  command. Regression test added; full suite passes (36 PASS).
- Test 6 built from clean archive of `f0a90d8`:
  `Swordfish-RC6-Test6-Portable.tar.gz`, 79,674,499 bytes, SHA-256
  `6c2784fe2e4e295302d400b389674b9d154f2a0ac1c152781f16ddee6367e28a`.
  Reports `0.0.1-rc6`; Xvfb startup passes. Test 5 download stopped.
- Test 6 result (10:58 UTC): USB HDD still DeviceBusy; `fuser -vm` was empty
  even while the error dialog was open, so the hold was transient. Cause
  found: the in-process `folder-size` thread walks folders recursively to show
  sizes. Changing directory only bumps its serial (checked every 256 entries)
  without waiting, so it was still inside the drive when udisksctl ran
  milliseconds later. Fix: `FileList::stopFolderScans(3000)` resets all lists,
  bumps a global scan generation (checked per folder and every 32 entries) and
  waits until no scan is active; called from `onNetworkUnmounted`. UDisks
  unmount also retries once after 1.5 s on DeviceBusy. Tests: transient busy
  succeeds on retry, persistent busy reports after retry, deep-tree scan stops
  quickly. Full suite passes (36 PASS).

- Source commit: `f69940aa71fe0e451d9f2d5aee22576510a55892`, pushed to
  `portable-layout`. Unrelated staged files and generated build files excluded.
- Built from a clean archive of that commit using
  `JOBS=1 ./portable/build-portable.sh` (Debian Buster, glibc 2.28).
- Download filename: `Swordfish-RC6-Test3-Portable.tar.gz` (about 76 MiB).
  SHA-256: `66f06ec2614a0d8a07b0e08d10ed046b98b68f126bbc2a6fdf5abd277c088762`.
- Archive listing passes; packaged executable reports `0.0.1-rc6` and stays
  running in an isolated Xvfb smoke test without startup errors.
- Delivered through a temporary private codespace HTTP download link, not
  published as a GitHub release. Stop the download server after the user
  confirms successful download/extraction. The source remains on GitHub.
- Prioritize testing duplicate mounts/login prompts; USB safe-remove button
  versus toolbar/context Unmount; grey remountable internal/USB devices;
  network-share Unmount; 1-second tab hover and left tab-bar visibility.

## 4. The archive-command / false idle-timeout bug

This is important enough to preserve separately.

The logs showed successful 30-second refresh cycles, followed by four cached
remote paths timing out at exactly about 60 seconds while the app was idle.
The user confirmed no sleep/network interruption, and new folders still opened
quickly. The screenshot alone had not established a network fault.

Root cause: [File::mount](src/File.cpp) temporarily removed `SIGCHLD` handling
so the global reaper would not consume `popen`'s child before `pclose`.
It then mistakenly restored `SIGINT`, not `SIGCHLD`.

Archive-folder open/save uses this wrapper. Remote helpers could finish, but
their exit status was never delivered to the remote queue; the queue correctly
waited for exit plus pipe completion, then eventually reported a timeout.

The fix restores `SIGCHLD` through a local scope guard on every return path,
restores immediately after `pclose`, and dispatches harvesting for children
that finished while signal handling was temporarily disabled.

A regression using a harmless archive-style command and the real application
signal handler failed before the fix and passed afterward. A manual test pump
that called `waitpid` itself would have hidden this defect, so the regression
intentionally does not rely on that pump for this sequence.

Do not "fix" a recurrence by merely hiding errors or increasing the timeout.

## 5. RC4 freeze and current implementation boundary

After Test5 acceptance the user asked to lock/release before another stage,
and to hide discovery only if working remote access would be preserved.
They approved **0.0.1 RC4**, not final 0.0.1.

Changes for the freeze:

- Create only Places, Bookmarks and Devices visible roots.
- Skip the absent discovery root during synchronization.
- Safely ignore the old toggle command when its root does not exist.
- Keep discovery code/helper, mounted Devices shares and Connect to Server.
- Update actual application version and user-facing release documentation.

The root-visibility and legacy-toggle regression passed with the complete
remote/archive tests. Full native and Debian 10 portable builds passed.

Only relevant application, test, dependency/build and documentation paths were
committed. Existing staged editor/customization files, source ZIPs, generated
autotools churn and translation churn were deliberately not swept in.

The first push was rejected because the new commit used a private author email.
With explicit permission, only the unpublished commit's author metadata was
corrected and its local unpublished tag recreated. Source-tree equality was
verified. No published release tag was rewritten.

The portable binaries came from a clean export of the identical source tree.
GitHub asset digests were checked against local checksums before publishing.
The release is a public **prerelease**, with older releases untouched.

### RC4 artifacts

| Asset | Bytes | SHA-256 |
|---|---:|---|
| `Swordfish-Portable-0.0.1-RC4.zip` | 84,035,751 | `f6ebf6497fdc0c961d178b012633175753ff9b54ce9e1da3ec909bc58111cc2e` |
| `Swordfish-Portable-0.0.1-RC4.tar.gz` | 79,081,003 | `adeb47b6db7c118390f1a8d7439ebc2f493cf2591c660a4d92ce1b56d660e0f7` |
| `Swordfish-FM-0.0.1-RC4-source.tar.gz` | 20,294,729 | `063f75045f5bf771c3906daad8f6d014648a9160c2c68439cb357133245e4d5e` |

`SHA256SUMS` accompanies these assets on the release page.
The full portable archives contain `Swordfish-Portable/` and seven executables:
`swordfish`, `swfa`, `swfi`, `swfp`, `swfw`, `swordfish-preview`,
`swordfish-network`. The first five/compatibility names have legacy origins.

## 6. Architecture map for the next developer/session

| Area | Main files |
|---|---|
| Startup, environment, signal registration | [src/main.cpp](src/main.cpp) |
| Main window, menus, shared state, child harvesting | [src/XFileExplorer.cpp](src/XFileExplorer.cpp) |
| File panes, navigation, opening, archive actions | [src/FilePanel.cpp](src/FilePanel.cpp) |
| Listings, metadata, Details expansion, views | [src/FileList.cpp](src/FileList.cpp) |
| Drawing, layout, tooltips and grid mechanics | [src/IconList.cpp](src/IconList.cpp) |
| Sidebar controller and backing-model synchronization | [src/DirPanel.cpp](src/DirPanel.cpp) |
| Places tree, bookmark/device models | [src/Places.cpp](src/Places.cpp) |
| Legacy directory Tree tab | [src/DirList.cpp](src/DirList.cpp) |
| Native remote queue, snapshots and subscriptions | [src/RemoteDirectory.cpp](src/RemoteDirectory.cpp) |
| Host-GIO listing and retained discovery helper | [src/NetworkDiscovery.cpp](src/NetworkDiscovery.cpp) |
| Preview queue/cache and decoder | [src/Preview.cpp](src/Preview.cpp), [src/PreviewWorker.cpp](src/PreviewWorker.cpp) |
| Preferences and installed-program choices | [src/Preferences.cpp](src/Preferences.cpp) |
| Associations, executable policies and launching | [src/FileDict.cpp](src/FileDict.cpp), [src/Execution.cpp](src/Execution.cpp), [src/xfeutils.cpp](src/xfeutils.cpp) |
| Open With and other command dialogs | [src/HistInputDialog.cpp](src/HistInputDialog.cpp) |
| File-selection sidebar | [src/FileDialog.cpp](src/FileDialog.cpp) |
| Archive/mount command wrapper | [src/File.cpp](src/File.cpp) |
| Categorized diagnostics | [src/AppLogger.cpp](src/AppLogger.cpp) |
| Portable launcher and container build | [portable/](portable/) |

### Remote-loader contracts to preserve

- New native handling targets descendants of `/run/user/<numeric uid>/gvfs/`.
  It is not a claim that NFS, CIFS kernel mounts or every FUSE path is async.
- The helper uses host GIO/GVFS to resolve a native remote URI and enumerate
  asynchronously in batches of 100.
- It returns names, type/mode, size, times, ownership where supplied, symlink
  information and access attributes. Missing ownership is not called "root."
- No serial per-entry FUSE stat/access/readlink in the native listing path.
- A shared queue serves multiple widget subscribers, preserves selection and
  existing rows during refresh, and updates FOX only on the GUI thread.
- Navigation, collapse and destruction release subscriptions. A shared job
  survives while another view needs it.
- Completion needs protocol completion, pipe EOF and valid child exit handling.
  The application harvester and all relevant companions dispatch helper exits.
- Active cached listings refresh about every 30 seconds or explicitly.
  This is polling, not a complete native filesystem monitor implementation.
- A 60-second helper deadline reports a failure; no silent slow-FUSE fallback.
- Paths and records must remain validated. Special filenames use an encoded
  transport rather than unsafe line splitting or shell interpolation.
- Host-only `swordfish-network` must not acquire bundle RUNPATH or load bundled
  old GIO alongside incompatible host modules.

Preview metadata/source sniffing and some other operations can still touch a
remote filesystem synchronously. Do not claim all application filesystem I/O
became asynchronous because listing did.

## 7. Build and validation recipes

Normal source build from the repository root:

```sh
./autogen.sh
./configure
make -j2
```

Focused rebuild during development:

```sh
make -C src -j2
```

Portable Linux x86_64 build:

```sh
DOCKER_BUILDKIT=0 JOBS=2 ./portable/build-portable.sh
```

The legacy-builder environment variable was used after a local BuildKit cache
problem; it is an environment workaround, not an application dependency.
The portable baseline is Debian 10 / glibc 2.28, not the Debian 12 baseline
used during earlier experiments.

Follow [portable/README.md](portable/README.md) for dependency and packaging
details. Do not hand-edit generated `configure`, `Makefile.in`, `aclocal.m4`
or `config.h`; regenerate them. Do not automatically commit unrelated generated
or translation changes.

### Checked-in focused regressions

There is no single universal unit-test/lint runner. These targeted tests exist:

```sh
bash tests/run-folder-expansion-regression.sh /absolute/path/to/Swordfish-Portable/usr/share/swordfish/icons/default-theme
python3 tests/native-directory-helper-regression.py ./src/swordfish-network
bash tests/run-network-discovery-regression.sh
bash tests/run-preview-regression.sh
/usr/bin/python3 tests/network-discovery-diagnostic-test.py
```

They require the development toolchain and appropriate Xvfb/decoder tools.
The folder test links existing application objects, so build first and use a
complete installed/portable icon directory, not an assumed complete source theme.

Coverage includes:

- 793 nested folders, local stat/access counts and no collapsed-child reads.
- Progressive delayed remote batches and bounded event-loop responsiveness.
- Selection/row retention during refresh and suspended interaction updates.
- Busy-cursor lifetime and release of desktop pointer grabs.
- Expansion/collapse/navigation cancellation, shared subscribers and stale callbacks.
- Native metadata, unusual newline/quote names and symlink/broken-link behavior.
- Helper failures, malformed records and EOF/exit ordering.
- Sidebar Network root behavior: no startup search, start on expand/title click,
  progressive server updates, stop/refresh and sorted "this computer" placement.
- Legacy tree deep-path synchronization.
- Real archive-style command followed by real signal-driven remote completion.
- Preview codecs, cache/invalidation, failed decoding, deadlines and child cleanup.
- Mocked discovery-record parsing/merging behavior; real LAN discovery was
  validated separately with the standalone diagnostic.

Additional view/zoom/sidebar/tooltip/association/terminal harnesses were run
from session artifacts during development. They are evidence of those tests,
but are not all checked in or guaranteed available in a new checkout.

### Important build/test lessons

- Never reuse host `.o` files in an old-glibc portable container. This produced
  real GLIBC symbol failures; the container script removes stale objects.
- Build releases from a clean committed export, not a dirty shared worktree.
- Do not edit sources while a portable source-copy tar is running.
- After shared C++ layouts change, stale objects can cause deceptive crashes;
  rebuild the relevant dependency graph rather than patching symptoms.
- Use the repository's GNU C++ conventions in GUI harnesses. Earlier strict
  language-mode changes affected platform macros and selector IDs.
- Run GUI checks in an isolated HOME/config and X11/Xvfb session.
- Some tests bypass global teardown because a pre-existing teardown problem
  is outside their scope. Passing them does not establish clean app destruction.
- One fixed-duration startup check was too short; bounded readiness polling
  correctly verified the window. Check readiness, not just a sleep.
- The preview video test has intermittently missed its readiness deadline
  under concurrent heavy builds and passed standalone. Preserve that caveat;
  do not hide a persistent failure by repeatedly rerunning it.
- `make -j4` can be killed for lack of memory on the 2-core/8 GB Codespace;
  use `-j2`. Dependency tracking is disabled, so run `make clean` after
  changing a header that alters a class layout (for example `FileItem`).
- If the portable build fails with "parent snapshot ... does not exist",
  the Docker BuildKit cache is corrupt: run `docker builder prune -f` (or use
  `DOCKER_BUILDKIT=0`) and rebuild.
- The Codespace root disk (32 GB, shared with Docker) can fill up during a
  portable build. `/tmp` is a separate, larger disk. After the builder image
  exists, run the container directly with `--volume /tmp/<scratch>:/tmp` added
  to the `docker run` line from `build-portable.sh` so compilation uses `/tmp`;
  `docker builder prune -f` reclaims stale build cache.
- The portable build downloads and checks LibRaw 0.21.5 by SHA-256; it needs
  network access to `www.libraw.org`. The container runs as the invoking
  (non-root) user, so LibRaw is installed into `/tmp/libraw-prefix` and found
  through `PKG_CONFIG_PATH`/`LD_LIBRARY_PATH`; installing into `/usr/local`
  failed with "Permission denied" in the first real container build.
- To exclude uncommitted work from a test build, export the commit first:
  `git archive HEAD | tar -x -C /tmp/<dir>` and run
  `portable/build-portable.sh` from that copy.

## 8. Portable operation, diagnostics and download recovery

Always launch **AppRun**, not `usr/bin/swordfish` directly. The launcher sets
libraries and isolated data paths while saving host settings for host commands.
The real HOME remains the user's home.

`portable-data` is intentionally absent from release archives and created on
first launch. Missing it immediately after extraction is not corruption.
An actual archive-manager error still requires checking download integrity.
Back up existing user data before replacing an installation.

Diagnostics have used more than one path over the project's lifetime:

- With `SWORDFISH_PORTABLE_DATA`, the current logger selects
  `<portable-data>/logs/`.
- With `XDG_STATE_HOME` and no portable override, it selects
  `<state-home>/swordfish/logs/`; uploaded user reports also came from
  `portable-data/state/swordfish/logs/`.
- Native fallback: `~/.local/state/swordfish/logs/`.

If uncertain, inspect the launched environment and locate `application.log`,
`file-operations.log` and `errors.log` rather than insisting on one historical
path. Other categories are `ui.log` and `settings.log`. Logs can include private
filesystem paths; review them before public posting.

Useful remote events include `remote-list-completed`, `remote-list-failed`,
`slow-directory-listing`, `slow-sidebar-expansion` and
`slow-tree-synchronization`. Distinguish loader durations from end-to-end time.

Temporary download service during development:

- Python HTTP server serving only the repository `build/` directory on port 8765.
- Codespaces forwarded hostname was
  `friendly-winner-jr7p6g7vvqj4fjpvr-8765.app.github.dev`.
- This address/process is temporary, not release infrastructure.
- Private visibility caused 401; a stopped server caused 502.
- Changing visibility to public required explicit user approval. The user
  changed it through the Codespaces Ports UI because CLI credentials lacked
  permission.
- Firefox intercepted Ctrl+Shift+P. Opening the Codespace editor's View menu
  and Command Palette with the mouse exposed the Ports view.
- Public access was verified after restarting the server. Return visibility
  to Private when no longer needed; do not expose unrelated files.

Prefer durable explicit GitHub release/tag links. `/releases/latest` skips
prereleases and must not be assumed to select RC4.

## 9. Unfinished work and agreements not to mistake for completed features

### 9.0 Current position (26 October 2026)

- **Latest:** Test 2 feedback reopened several bugs; see section 3.16.
  Test 4 is now built from `91fa099` for limited no-selection/inventory/spare
  USB testing; delivery details and checksum are in section 3.16.
  **Test 3 is withdrawn:** unsafe Unmount targeting, disappearing unmounted
  devices and a reported system freeze. User will discard it. Download server
  stopped. Prioritize explicit safe Unmount targeting and investigate host
  volume inventory before building again; freeze cause remains unknown.
  Network, device controls, visible device-row actions and tab corrections
  pass the regression suite and form the RC6 Test 3 bundle. The user authorized
  its commit/push and clean portable build; real-network/hardware validation
  remains pending. See the delivery record in section 3.16.
- **Previous delivery:** the RC6 bug list in section 3.15 was implemented and committed as one
  bundle; the user will test the RC6 Test 2 package on 26 October with the
  checklist below. Group 4 below is paused until the test results are in.
  A2 (double mounts) has now recurred and is under investigation.
- Test checklist: USB mount/unmount/eject and unmount-in-Thunar; internal
  partition remount and the Mount/Unmount buttons; no Thumbnails setting and
  zoom works; path-editor right-click menu and Ctrl+L; About/Help links;
  tab drag-hover at 1.5 s; slow-click rename anywhere in the Name cell; drag
  auto-scroll and drop selection; Windows server without password, Connect to
  Server (type + server only, then with a share), `share@computer` names.
- Branch `portable-layout`, all work pushed (latest commit is this history
  update, after `96f0c80`). Version string
  `0.0.1-rc6`, but **RC6 is not released**. Build RC6 only when the user asks.
- **RC6 Test 1** is a TEST ONLY GitHub prerelease (`swordfish-rc6-test1`)
  built from a clean export of the application source at `7103fe4`, with the
  portable build-script fix committed right after it, so the user and tester
  can check the section 3.14 work. RC5 and the listing diagnostic prerelease
  stay unchanged.
- Commit `d85ad73` accidentally added two packaging lines for the
  uncommitted listing-diagnostic launcher to `build-portable-in-container`;
  they were removed from the committed script. The working copy still has
  them for diagnostic builds.
- To-do groups 1, 2, 3 and 6 are done. Group 4 (file list) is in progress.
  Remaining in group 4, in order:
  1. Auto-size columns (double-click the header divider; item 14).
  2. Column show/hide right-click menu on the header (item 19).
  3. Draggable (reorderable) columns (item 20).
  4. Remember view settings per folder.
  5. Thumbnail placeholders: deferred by the user.
- Then group 5 (settings/themes) and group 7 (big features).
- The incomplete-listing report was closed on 25 October as a false alarm
  (the tester compared two similarly named folders). Its trace code was then
  kept and committed behind a new **log level** setting (Preferences >
  Settings > Logging: Off / Errors only / Normal / Detailed / Debug, stored as
  `OPTIONS/log_level`, default Normal, applied immediately and inherited by
  child programs through `SWORDFISH_LOG_LEVEL`). Detailed writes per-folder
  summaries and Debug writes every entry to `listing-diagnostic.log`;
  `SWORDFISH_LISTING_DIAGNOSTIC=1` still forces Debug. The separate
  listing-diagnostic test package (`portable/LISTING-DIAGNOSTIC.md`,
  `portable/run-listing-diagnostic` and its two packaging lines) was never
  committed and was deleted on 25 October.
- Open items: discovery of unplugged or
  unmounted volumes; the uncommitted device/automount hunks (in
  `src/Places.cpp`, `src/XFileExplorer.cpp`, `src/xfeutils.*` and the
  "Local devices and automount" section of `portable/README.md`) await approval.
- The local `configure`/`config.h` in the main checkout are stale (no `PACKAGE`
  define), so `make` there fails in `Preferences.cpp`. Validate in a scratch
  copy using the committed `configure`, `aclocal.m4` and `src/Makefile.in`.
- Uncommitted but deliberately left alone: regenerated `configure`,
  `aclocal.m4`, `src/Makefile.in`, `tests/folder-expansion-regression.cpp`,
  and many staged generated/translation files. Never `git add -A`, reset or
  clean. Commit explicit paths (or explicit hunks through a temporary index).

Development test loop used in this cycle: an extracted RC5 bundle at
`/tmp/tabrepro/rcu/Swordfish-Portable-0.0.1-RC5` (temporary, may be gone),
`src/swordfish` copied into its `usr/bin/`, launched under Xvfb display `:91`
with Fluxbox, driven with `xdotool`/`wmctrl` and checked with screenshots.
Clear `<bundle>/portable-data/cache/swordfish/previews` for fresh preview tests.

### Discovery and network management

RC4 kept discovery hidden, but the next-cycle implementation now exposes a
Network root in the sidebar and runs `swordfish-network --discover` on demand:
expand the root, click the Network title, or click Refresh. It does not run at
startup. Records are progressive (`SERVER/STATUS/DONE`), merged, sorted and
validated before UI display. Discovery itself never mounts or prompts.

Current validated scope:
- Linux/Samba hosts from host GVFS (`network:///` and `dns-sd://local/`).
- Windows hosts from WS-Discovery + metadata Get + TCP 445 open check.
- Only file-sharing machines (port 445 open) are shown.
- Local machine is labelled "(this computer)" and listed last.
- Sidebar tooltips include address, workgroup (when available) and discovery
  source(s).

Still pending by design:
- Printers and media servers under separate headings.
- Real LAN validation of the new share-tab and selected-share mounting flow.
- Windows 10/11 confirmation on real LAN runs.

Network Test 1 found all three computers on the user's LAN, but connecting
through the legacy Connect dialog failed with
`gio: undefined symbol: g_unix_mount_get_root_path`. Its host `gio` process
inherited the portable bundle's older GLib libraries. The dialog's SMB/SSH
mount and keyring commands now use `xf_hostcommand` around the entire shell
pipeline, restoring host library, GIO and desktop settings. This corrects the
reported loader failure; successful LAN mounting still needs user validation.

The user then approved a different browsing workflow: single-click a computer
to list its shares in a normal tab, double-click one share to mount/open only
that share. Share listing uses asynchronous host GIO, with anonymous access
first and an authentication dialog only when requested by the server. Named
users can submit blank passwords. A server tab is a virtual, read-only list,
not a local filesystem directory; destructive actions and drag/drop are blocked
until a share is opened. Server tabs participate in tab switching and history.
Typed `smb://server/` addresses also use this flow rather than a share chooser.
Focused tests cover anonymous listing without any share mount, credential
requests/cancellation, blank passwords and exactly one selected-share mount.

Network Test 2 LAN report: remote Linux shares listed and opened (already
mounted or stored credentials); Windows asked for a login. Opening a share on
"this computer" left a permanent busy cursor and froze the window. Mounting
ran synchronous `gio` commands on the GUI thread, and `gio mount` can keep
re-asking for credentials on a closed stdin. Every share-mount command in
`FilePanel::resolveNetworkPath` (`gio info`, guest/authenticated `gio mount`,
`secret-tool search`) now runs through `networkRun`, bounded by host
`timeout` (10 s info/guest, 20 s authenticated mount). A timeout reports
"The server did not respond in time." in the credentials dialog instead of
hanging. A regression (fake `gio` hanging for `hang.test`) checks this. This
is a bounded freeze, not a fully asynchronous mount.

Network Test 3 built successfully and passed the GUI regression, including
the hanging-mount timeout test, and a packaged launch smoke check. The user
confirmed that opening Data on this computer displays the login dialog after
roughly 20 seconds, then mounts and opens successfully with domain MAKHLOUF
and the user's Samba credentials. Opening an already-mounted share also works.
The remaining connection UX work is to reduce the pre-login delay and keep
the window responsive while connecting.

Windows login testing is explicitly deferred at the user's request until
Windows shares are reviewed/configured, possibly on Windows 10 or 11.
It is not a prerequisite for continuing the Linux connection UX work.

The next LAN report exposed a separate unmount problem: Swordfish reported
"successfully unmounted" while the share remained mounted in both Swordfish
and Thunar. The file pane used the configured ordinary `umount` command for
GVFS paths, and `File::mount` ignored nonzero command exit codes when displaying
success. The shared runner now recognizes GVFS roots (including descendants),
uses host `gio mount -u` with a 30-second timeout, checks command status and
requires the GVFS entry to disappear. Ordinary mounts retain the configured
commands. Failed operations no longer navigate away or display success.
Successful network unmounts redirect affected active/inactive tabs in both
panes, and the directory tree, to the home folder before the success message.
The shared command runner remains linkable by the companion applications.
Regression coverage includes command failure with informational messages
disabled, a zero-exit command leaving the mount present, host environment
restoration, URI reconstruction and active/inactive tab cleanup.
The full native build and GUI regression passed. Network Test 4 was built
against Debian 10 on temporary storage and passed packaged help/GUI launch
checks; real LAN unmount validation is still pending. No source changes have
been committed.

Separately, unmounting through Thunar produced an error in Swordfish even with
warning messages disabled. External-unmount handling has not yet been changed;
the exact error still needs inspection. The pre-login delay is also unchanged.

Network Test 4 LAN validation confirmed that unmounting in Swordfish succeeds
and removes the share from Thunar. Swordfish, however, retained multiple stale
5TB Devices entries while only 4TB remained mounted; refresh did not remove
them, but restarting did. Mount-list rebuilds now clear all old rows rather
than removing rows by dictionary label, preventing accumulated stale entries.
Permission/read errors do not masquerade as an empty mount list.

The user approved closing tabs for unmounted shares rather than redirecting
each tab Home. Both panes now close affected active/inactive tabs and subfolder
tabs, retain unrelated tabs, and keep one Home tab only if no tabs survive.
Devices snapshot removals trigger the same cleanup for external unmounts.
A remote-list error caused by a vanished mount invokes cleanup instead of
showing the expected disappearance as a folder-loading failure. Opening an
already-mounted share reports "Share is already mounted at ..." with its
local path, reusing the existing mount without a new mount command.
The native build and GUI regression passed, including external removal,
repeated mount/unmount cycles, tab closure/Home fallback, existing-mount
notification and the earlier failure/status tests. Pre-login delay and
Windows testing remain unchanged/deferred.
Network Test 5 was packaged against Debian 10 using temporary storage, passed
packaged help/GUI launch and runtime-path checks, and is available as
`build/Swordfish-Portable-Network-Test5.tar.gz`. The user reported successful
LAN testing of Network Test 5 on 2026-10-18.
All changes remain uncommitted.

About and Help now provide a clickable project repository URL, opening
`https://github.com/hasmak-44/Swordfish-FM` through host `xdg-open` rather
than inheriting portable libraries. The duplicated plain URL in About was
removed; existing copyright and licence notices are unchanged. The native
build and GUI regression passed, including activating both links with a
fake host opener. This addition is not in the already-built Network Test 5
archive and will need packaging before publication.

The user approved attribution as Hassanein Makhlouf (hasmak-44), with
AI-assisted development, without asserting joint copyright in upstream work.
About, Help and the README now carry this credit while retaining existing
copyright/licence notices and acknowledging Xfe's contributors. Project
communications use GitHub Issues; no personal email is added.

Publication approved on 2026-10-18: commit the post-RC4 networking,
unmount/tab lifecycle, attribution and project-link changes on portable-layout,
then publish a separate Network Preview 1 prerelease. The frozen RC4 tag and
assets must remain unchanged. Preview limitations include the pre-login delay
and Windows validation deferred until the user's Windows shares are ready.

### Application and executable UX

Earlier discussion proposed removing the legacy content-app suite entirely,
text-only generic Edit, a simplified two-choice executable-text policy and
system-first fallback behavior. Parts of the broader opening/settings redesign
were implemented, but not all of that final simplification.

RC4 still builds the legacy companions and currently retains the three
Always run / Ask / Don't run script choices. Do not describe their proposed
replacement as shipped. Reconfirm the intended next-stage behavior and trace
all panes/search/menu entry points before changing it.

### Broader roadmap

Discussed, not blanket implementation claims:

- Persistent context menus and deliberate right-button drag/drop semantics.
- Customizable menus and well-scoped custom actions.
- Operation centre/queues, conflict handling and clearer progress.
- Honest, limited undo rather than unsafe promises.
- Bulk rename previews.
- Pane comparison and search results as useful working views.
- Checksums and small navigation improvements.
- Longer-term separation of toolkit-independent behavior from FOX.

Avoid speculative automatic synchronization, a large plugin framework or a new
application suite without renewed requirements.

## 10. Release and continuity discipline

At the end of this session, unrelated staged files still existed, including
editor/customization files, source ZIPs, generated autotools files and
translation changes. Some paths had both staged and unstaged edits.
That is not permission to commit, delete or reset them.

For another release:

1. Agree scope/version and inspect the worktree.
2. Validate the actual change and directly related behavior.
3. Commit only intended paths, preserving unrelated staged content.
4. Check author privacy and signing availability before attempting publication.
5. Build from the exact committed tree or a verified identical tree.
6. Verify version, GUI readiness, helper linkage, archive contents and checksums.
7. Exclude user settings/cache/logs from binaries.
8. Include matching source and required third-party notices.
9. Verify uploaded digests before publishing.
10. Record user acceptance separately from local automated tests.
11. Update this guide when decisions, limitations or the stable baseline change.

The local GPG signing key was unavailable. Per-command signing overrides were
used rather than changing global configuration. GitHub email privacy rejected
new private-address commits; metadata-only amendments required explicit approval.
Do not amend published commits/tags as an automatic workaround.

### Durable starting point

RC4 is the last frozen application release in this document. It contains the
tested remote-loading improvements and archive child-handler fix, with unfinished
Network discovery hidden. Start the next stage from that understanding, not
from an old experimental ZIP, an obsolete roadmap or an incomplete chat summary.
