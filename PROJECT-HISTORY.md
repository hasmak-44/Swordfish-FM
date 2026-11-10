# Swordfish FM: project history and recovery guide

Last updated: 10 November 2026, through the 0.0.3 beta 2 release (section 3.56).
Latest release: `swordfish-0.0.3-beta2`.
Stable fallback: `swordfish-0.0.2`.

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
3. The current published stable baseline is **0.0.2**:
   - Repository: <https://github.com/hasmak-44/Swordfish-FM>
   - Development/default branch used for this work: `portable-layout`
   - Release tag: `swordfish-0.0.2`
   - Application commit: `30a9a315476a9cf358560b5beba27f6c00a3fac7`
   - [Stable release](https://github.com/hasmak-44/Swordfish-FM/releases/tag/swordfish-0.0.2)
4. Treat published releases as locked. Do not move their tags, replace their
   assets, or present local test builds as a new stable release.
5. The **Network discovery section is intentionally hidden**, not accidentally
   missing. Mounted shares and Connect to Server remain available.
6. The native remote loader is a major verified improvement. Do not remove
   `swordfish-network` just because discovery is hidden: its `--list` mode
   provides the fast mounted-share loader.
7. User-confirmed fixes include the portable search warning, child-window
   icons, missing-archivemount guidance and the icon-startup fix (section 3.40).
   The subsequent Custom Action, device and custom-toolbar changes have local
   automated tests but still need the user's real-machine testing.
8. Before claiming a feature is implemented, distinguish code, automated
   validation, portable validation and the user's real-machine confirmation.
9. For the latest state, read sections 3.40-3.50. The RC4-RC6 notes are
   historical context, not the current release or pending-work status.

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
- Test 7 built from clean archive of `c327bb6`:
  `Swordfish-RC6-Test7-Portable.tar.gz`, 79,675,723 bytes, SHA-256
  `6848ef55051fe1d57106a2078de3103cb01472fceb6b7ee7c222386300e992c1`.
  Reports `0.0.1-rc6`; Xvfb startup passes. Test 6 download stopped.
- Test 7 result (11:17 UTC, user hardware): USB HDD Eject succeeds; Unmount
  also clean. USB busy problem resolved. Still untested: internal-partition
  unmount, and whether the desktop stays responsive (freeze hypothesis).
- Test 7 internal partition (11:19 UTC): Unmount asks for the admin password
  (polkit), unmounts, row stays listed greyed, click remounts. Taskbar, dock
  and Start menu stayed responsive throughout (user confirmed, no freeze).
- Test 7 network findings (11:56 UTC): left tab bar loss did not recur
  (treated as one-off). No password prompt for Windows (192.168.1.102,
  confirmed by user via ipconfig) or mx-hp.local. All GVFS mounts lack
  `user=` (Windows: data-1, x-recoverd, movies on dell, prog on dell), and
  the keyring has no Windows entry, so these are no-username (guest or
  anonymous) connections from Swordfish's guest-first attempt, not saved
  credentials. Windows still allows editing a text file although Guest is
  read-only in Advanced Sharing; effective Windows account not yet confirmed
  (`net session` pending). Likely Windows permissions (e.g. Everyone with
  Change, or NTFS), not a Swordfish bypass. Proposed option: stop automatic
  guest attempts and always ask for login with a Guest choice.
- User decision (12:00 UTC): keep guest-first SMB behaviour unchanged; it
  connects and disconnects smoothly. Write access is a Windows permission
  matter, not a Swordfish change.

### 3.17 Swordfish 0.0.1 stable release: 26 October

User approved Test 7 as the first stable release (12:06 UTC) and chose the
new tag `swordfish-0.0.1` (old tags `SwordFish-Portable-0.0.1`, `v0.0.2`,
`v1.0-swordfish-fixed` from early October kept untouched) and updating main.
- Release commit `7496f0701215c80bf5e0bdde7d724750db5bfcfe`: `configure.ac`
  version `0.0.1`, README download section and CHANGELOG entry.
- `main` fast-forwarded from `0a96502` to `7496f07`; annotated tag
  `swordfish-0.0.1` pushed.
- Clean `git archive` build, `JOBS=1`, Debian 10 / glibc 2.28. Packaged
  `AppRun --version` reports `swordfish version 0.0.1` (zip and tar.gz);
  Xvfb startup passes. Top-level folder `Swordfish-Portable-0.0.1`.
- GitHub release https://github.com/hasmak-44/Swordfish-FM/releases/tag/swordfish-0.0.1
  (not prerelease, marked Latest). Assets and SHA-256:
  - `Swordfish-Portable-0.0.1.zip` 84,806,047 `559d86cb9495781fc8469579ba4d184f45ddbc14e7a8b762dbd80971e4bdca88`
  - `Swordfish-Portable-0.0.1.tar.gz` 79,674,731 `a7d5adf3f27a45a0fb5e249ee53a740aeab0b7c41e1e3f02971eb88010f2a2d1`
  - `Swordfish-FM-0.0.1-source.tar.gz` 20,391,491 `e667b4f78d3b37f1cd946e04e91f3c4e85997733fe7d0d4297f156e5edc0f10d`
  - `Swordfish-0.0.1-SHA256SUMS`
  Downloaded back from GitHub and verified.
- The `portable-release.yml` workflow only runs for `swordfish-portable-*`
  tags, so no automatic `Swordfish-Portable.zip` was added and GitHub Pages
  still serves the older automatic build.
- Pitfall: this checkout has an `upstream` remote (roland65/xfe); always pass
  `-R hasmak-44/Swordfish-FM` to `gh release` commands.
- Test 2–7 download servers are all stopped.
  Fixes the Test 3 faults (no password prompt, row disappeared, freeze).

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

### 3.18 Disengaging from Xfe: 26 October

User asked to fully disengage from Xfe while keeping the release safe and
published. GitHub never listed the repository as a fork; the only link was the
local `upstream` remote (roland65/xfe), now removed from the codespace checkout.
- `src/foxhacks.cpp`: non-portable installs no longer search `/etc/xfe`,
  `/usr/share/xfe`, `/usr/local/share/xfe` or `xfe` along PATH, and no longer
  parse a desktop-wide `xferc`. Order is now the executable's own
  `../share/swordfish`, then the first of `/etc`, `/usr/share`,
  `/usr/local/share` + `/swordfish`, then `~/.config/swordfish`. Portable
  mode is unchanged. Verified with strace under Xvfb against a temporary
  `/usr/share/xfe/xferc`: the old build opened it, the new build did not.
- Removed Xfe's Debian packaging (`debian/`, source package `xfe`) and Xfe's
  `.run` scripts (`buildrun`, `installrun`, `uninstall-xfe`) which install or
  delete `/usr/local/bin/xfe` and `/usr/local/share/xfe`. `Makefile.am`
  `EXTRA_DIST` also dropped the never-existing `uninstall-swordfish`;
  `Makefile.in` regenerated with `automake --gnu Makefile` (only those lines
  change).
- AUTHORS names Swordfish FM first and keeps the Xfe, FOX and X Win Commander
  credits (GPLv2 obligation). README configuration section and
  `swordfishrc.in` comments now describe Swordfish paths.
- Deliberately unchanged: the internal FOX application name is still `xfe`
  (`src/main.cpp`), so per-user settings live in `~/.config/swordfish/xferc`.
  That file is inside Swordfish's own folder; renaming would discard existing
  user settings, so it needs a migration if ever changed. Internal source
  names (`xfedefs.h`, `xfeutils`, `XFileExplorer`, `xfe*.png` icons, the
  source file `automount/xfe-automount.cpp`) are also unchanged.
- The unpublished local `install-swordfish.sh` (copies `/usr/bin/xfe` to
  `/usr/bin/swordfish`, forces `Icon=xfe`) was never committed; it explains how
  the user's very old system install became tangled with Xfe.
- Validation: clean archive build + `make install DESTDIR`, folder-expansion
  suite 36 PASS, preview suite PASS (one timing failure under build load,
  passed twice on rerun). The network-discovery `shareAuthentication`
  assertion also fails on the pre-change build (stale test after guest-first
  SMB decision) and is not caused by this change.
- No new binary release: the portable bundle behaves identically, so
  `swordfish-0.0.1` remains the current download.

Follow-up the same day (automounter), found while cleaning the user's machine:
- `dpkg -V xfe` on the user's machine showed `/usr/bin/xfe-automount`
  modified: Swordfish installed its helper under Xfe's name and overwrote it.
  At startup Swordfish also ran `pgrep xfe-automount` and, with automount off
  (the default), `killall -q xfe-automount`, so it stopped Xfe's automounter.
  The helper opened drives with `xfe` and read `~/.config/xfe/xferc`.
- Fix: program renamed to `swf-automount` (13 characters, under the 15-char
  process-name limit used by `pgrep`/`killall`); `pgrep -x -u user`,
  `killall -q -u user`; helper opens drives with `swordfish` and reads
  `~/.config/swordfish/xferc` (where Swordfish's `automount_open` is saved);
  paths are shell-quoted (spaces in drive names previously broke "open a
  window"). Portable build list and READMEs updated; `automount/Makefile.in`
  regenerated with `automake --gnu automount/Makefile`.
- Verified with UDisks2 dev headers installed in the codespace (a clean
  rebuild is required after re-running configure, otherwise
  `XFileExplorer.o` keeps the build without `XFE_AUTOMOUNTER`). With stand-in
  processes under Xvfb: `xfe-automount` survives Swordfish startup,
  `swf-automount` is stopped when automount is off. Quoting tested with spaces
  and `'; touch` injection. Folder suite 36 PASS, preview PASS.
- This changes portable behaviour (the bundle ships the helper), so it needs
  a new release to reach users.

User machine cleanup (26 October, guided step by step):
- Very old system install (Oct 5, `/usr`, plus leftovers in `/usr/local`):
  `/usr/bin/swordfish`, `swfa/swfi/swfp/swfw`, `/usr/share/swordfish`,
  5 desktop files, 5 man pages, `org.swordfish.root.policy` (both prefixes),
  Papirus/hicolor/pixmaps icons, `xfe.svg.swordfish-backup` (Papirus
  `xfe.svg` itself verified unmodified) and `swordfish.mo` translations
  (both prefixes) removed. No dpkg package owned them.
- Xfe purged and reinstalled from the user's .deb, upgraded from 0.11 to
  0.13; `dpkg -V xfe` OK; Xfe kept the user's `~/.config/xfe` settings.
- Kept: `~/.config/swordfish`, the user's menu entry
  `~/.local/share/applications/swordfish.desktop` (runs
  `~/Apps/Swordfish-Portable/AppRun`; its icon path points to the Projects
  drive) and two test entries. Swordfish starts from the menu.

### 3.19 Swordfish 0.0.2 release: 26 October

User tested `0.0.2-test1` (built from `1a4b335`, delivered by a temporary
codespace link the user started himself because the agent was denied
starting the server): About showed the version, folders/USB Eject/network
Unmount fine, Xfe kept working with Swordfish open, automount mounted a
drive and opened a Swordfish window. Desktop (Thunar/Xfce) automount raced
with Swordfish's; advice: enable only one automounter. Not a regression.
- Release commit `30a9a315476a9cf358560b5beba27f6c00a3fac7`: version 0.0.2,
  README banner/download section, CHANGELOG heading. main and
  portable-layout fast-forwarded; annotated tag `swordfish-0.0.2`.
- Clean `git archive` build, `JOBS=1`, Debian 10 / glibc 2.28.
  `AppRun --version` = `swordfish version 0.0.2`; Xvfb smoke passes.
  `portable-data` (created by the build script) removed before packaging,
  per the release rule; archives verified to contain no `portable-data`,
  to contain `usr/bin/swf-automount` and no `xfe-automount`.
- GitHub release https://github.com/hasmak-44/Swordfish-FM/releases/tag/swordfish-0.0.2
  (Latest, not prerelease). SHA-256:
  - `Swordfish-Portable-0.0.2.zip` 84,803,698 `e0b2a3a98ad0a3c781000761081c6a218579361fee09e4dbb83835515f4c03fb`
  - `Swordfish-Portable-0.0.2.tar.gz` 79,671,263 `d101ddda664f057bb3b96fab537c2a64c83171e029967dec64c87b9366f39a82`
  - `Swordfish-FM-0.0.2-source.tar.gz` 20,409,285 `c68484f8e16495c9fc16a8cdc53aacd758e3fb6333fb5fd4e77a0cd2a4c92061`
  - `Swordfish-0.0.2-SHA256SUMS`
  Downloaded back from GitHub and verified.
- 0.0.1 release stays published. Pages still serves the older automatic
  build (workflow only triggers on `swordfish-portable-*` tags).
- The `upstream` remote no longer exists; still pass `-R` to gh.

### 3.20 Automatic release build fixed: 26 October

`portable-release.yml` only triggered on `swordfish-portable-*` tags, so
0.0.1 and 0.0.2 were packaged by hand and its Pages step was stale (the
website is a legacy Pages build of the `portable-layout` README, which
links to the latest release). Commit `9d834f8`:
- Also triggers on `swordfish-[0-9]*` tags; such a tag must equal the
  `configure.ac` version. Manual "Run workflow" takes an existing tag.
- Builds in the Debian 10 container and packages the same four files as
  the hand-made releases (`Swordfish-Portable-<v>.zip`/`.tar.gz` with top
  folder `Swordfish-Portable-<v>`, `Swordfish-FM-<v>-source.tar.gz`,
  `Swordfish-<v>-SHA256SUMS`), with `portable-data` removed and checked.
- Creates the release if missing (notes = matching CHANGELOG section;
  rc/test/preview/beta/alpha become prereleases, not Latest). If the release
  already has any of the files, uploads nothing (no replacing, no mixing).
- Pages deploy steps removed so the README website is not overwritten.
- Tested: run 40336388846 on `swordfish-0.0.2` built all files and left the
  verified 0.0.2 release files unchanged. CI builds are not byte-identical
  to hand builds (timestamps), which is why existing files are never mixed.
- Future release: rename CHANGELOG `## Unreleased` to `## swordfish-<v> — ...`,
  set `configure.ac`, commit, push the annotated tag; the workflow publishes.

### 3.21 Local usability fixes and improvements-test3: 28 October

These changes are local and uncommitted after `8a09b64`. The user requested
a test build and history update, **not a push, release or stable-version bump**.
The application still reports 0.0.2; the archive name identifies the test.

- Search now restores the host library environment before running find/grep,
  fixing the bundled libselinux version warning. Child windows/dialogs receive
  the Swordfish taskbar icon. The user confirmed both fixes in bugfix-test1.
- File-panel and search context menus separate Rename (one selection) from
  Bulk Rename (multiple selections). F2 remains selection-aware and unchanged.
- Devices have independent stop/go and eject controls: red stop for Unmount,
  green go for Mount/Remount, and red eject matching the user's screenshot.
  Internal unmountable partitions have no eject control; Root/Home stay
  protected. USB eject releases all partitions before powering off the drive
  and aborts on failure. No forced unmount or application termination is used.
- Supported SMB/SFTP shares can be unmounted and remounted through the existing
  authentication flow. Their disconnected entries survive for the current
  session only. Eject disconnects/forgets the entry, never stored credentials.
- Busy failures identify processes visible through /proc where possible and
  say "Please close all applications using the device and try again."
  If the filesystem remains mounted, failed unmount restores affected tabs,
  including both panes' order, selected tabs and view modes. This work was
  paused at the user's request, then verified when preparing this test build.
- Open Archive as Folder remains available for supported archives, including
  ISO, when archivemount is absent. A consent prompt offers the upstream
  installation page in the host browser. Nothing is installed automatically;
  archivemount and privileged host FUSE support are not bundled.

Validation before packaging: full native make, search-window regression suite
(search environment, icons, rename guards, archive menu/browser consent), and
folder-expansion regression suite (device actions, remount authentication,
multi-partition eject, protected mounts, real busy-process identification,
and failed-unmount tab restoration) passed. The restored-folder cursor check
now waits up to three seconds for asynchronous loading rather than asserting
before the load completes. Earlier fixed-delay remote-loading checks have
occasionally failed under load; do not hide failures by assuming a pass.

Test archive: `build/Swordfish-Portable-0.0.2-improvements-test3.tar.gz`;
matching checksums: `build/Swordfish-Portable-0.0.2-improvements-test3-SHA256SUMS`.
Built with the Debian 10 portable builder (glibc 2.28). Docker initially had a
missing cached layer; rebuilding this image with `--no-cache` fixed packaging
without deleting shared Docker data. The package reports 0.0.2 and its visible
main window appeared and survived resizing under Xvfb. All 42 folder/device
checks and six search-window checks passed. Runtime `portable-data` is removed
before final packaging; it is recreated on launch. Real USB/share behavior and
button colours still need the user's test; automated fixtures are not acceptance.
The earlier devices-test2 archive does not include busy-tab restoration,
archive guidance or the button colours.

Delivery: served temporarily from the codespace on port 8765 (SHA256
`fada0a3733c2c2d850cf4272f2badadd4a587608c9996fd02c7aef745ff43d39`).
On 28 October the user downloaded, extracted and launched it successfully;
the server was then stopped. Real-machine testing is in progress. Next step:
wait for the user's Test 3 report (device colours, USB/share unmount, remount
and eject, busy-device message/tab restoration, rename entries, archive help).

Pending separately: beginner manual, then custom actions/custom toolbar;
native Wayland/cross-platform investigation. No port is claimed. Preserve
the saved local-change backup and user screenshots; do not reset the worktree.

### 3.22 Preferences scrolling and folder new-tab action: 29 October

Before starting the agreed beginner manual, the user requested two local
changes: Open in New Tab for a selected folder, and investigation of slow /
apparently incomplete Preferences scrolling (General and Appearance; the
theme list itself already scrolled).

- File-panel context menus now offer Open in New Tab for one selected folder.
  It activates a new tab in that pane, preserves the original tab and observes
  the existing tab limit. Files, multiple selections and the parent entry
  cannot invoke the action.
- Preferences page scrollbars previously used FOX's default one-pixel line
  increment. They now use font-sized increments. Regression checks at a
  reduced dialog height cover wheel movement, both ends of each overflowing
  page, visibility of the bottom control group, and independent nested-list
  scrolling. A separate range-truncation bug has not been reproduced.
- The regression runner uses the application's configured compiler flags,
  since Preferences has conditional fields that must match the linked object.

These are local source changes, not a new portable build or user acceptance.
The existing downloaded Test 3 archive is unchanged. No push or publication
was requested. Other discussion items remain pending; the future complete
customization engine is explicitly exploratory, not an implementation task.

### 3.23 Agreed Beta 1 usability, welcome and manual: 29 October

The user clarified that all agreed changes should be applied together, not
only the manual. The next local version is **0.0.3-beta1**; stable 0.0.2 and
the downloaded improvements-test3 archive remain unchanged. No commit, push
or publication is authorized by this implementation agreement.

Implemented locally:

- Unmounted device names use normal readable theme text with an
  `(unmounted)` suffix. The visible suffix is not part of the device's stable
  identity; the green Mount button remains available.
- Each tab remembers its own column widths, keyed by column identity rather
  than position. Widths follow switching, reordering, tab removal, restored
  sessions and failed-unmount tab restoration.
- A Preferences button at the right of the menu bar opens the existing
  dialog. The current theme supplies a tools/wrench image, not a new icon
  set or a complete UI customization engine.
- File and search context menus group edit/rename actions separately from
  copy/move/link actions, without new submenus. Copy Path replaces Copy Name
  and copies plain full paths, one per selected item. Other Pane commands
  use the destination pane's active tab; Link joins Copy and Move.
- The selected-folder new-tab command and readable Preferences scrolling
  from section 3.22 are included.
- Help / User Manual opens the bundled offline HTML in the host browser.
  The manual has 28 beginner chapters, real application screenshots, an
  annotated workspace and two explanatory drawings. It assumes no previous
  computer knowledge. Its HTML/images can also be hosted online; no online
  publication or separately maintained PDF is claimed.
- New personal settings profiles receive a welcome with Read User Manual,
  Next and Start Using choices. The next page offers Customize and System
  Integration. Existing settings suppress the welcome. Detection checks
  personal settings files, not just FOX's successful bundled-default read.
- Preferences has a separate System Integration page. Desktop shortcut,
  applications-menu entry and personal command link all start unchecked.
  Changes are per-user; portable shortcuts invoke AppRun. Ownership checks
  protect unrelated files, edits and symlinks. Removing a selection removes
  only an unchanged Swordfish-created item. Host PATH collisions are refused.
  Instructions explain desktop Allow Launching, PATH visibility and repairing
  links after moving a portable folder. Errors keep Preferences open.

Validation so far: full native autotools build, search/UI regressions (including
actual copy, move and link into the other pane's active tab), integration
ownership/collision tests, and manual link/image checks pass. An isolated
native first-run launch showed the welcome once and not on the next launch.
The folder/device suite subsequently passed in full with its strict 150 ms
single-event latency limit unchanged. Earlier runs hit that limit at 154-159 ms
on this shared development machine. Complete portable validation and the user's
real-machine acceptance must be recorded separately.

Future custom actions, toolbar customization and a hierarchical UI styling
engine remain discussions, not Beta 1 implementation.

### 3.24 PATH destinations and live Details columns: 29 October

The user approved implementation after discussion:

- Do not add a directory to PATH. Offer existing host PATH directories instead,
  preferring writable locations; create a missing personal `.local/bin` only
  when it is already listed in PATH. Show the full link path and whether it is
  personal/shared or needs administrator authorization before applying.
- Protected standard command destinations use explicit `pkexec` system
  authorization, never an application password box. A small non-setuid
  `swordfish-command-link` helper is installed in libexec and bundled for portable
  use. It restricts destinations and actions, validates targets, refuses
  unrelated links and keeps root-owned per-user ownership records. Protected
  removal requires authorization too. Cancel/error keeps Preferences open.
  Desktop and applications-menu entries remain per-user.
- Right-click a Details heading for a show/hide checklist. Drag the middle of
  one heading onto another to reorder immediately; widths follow their column.
  Name remains visible and first. Search retains Folder Name; Trash uses the
  fixed Caja-style Name, Size, Type, Trashed On and Original Location columns.
  Existing Preferences controls remain available.
- Tabs persist column order/visibility as well as widths, including reordering,
  closing and failed-unmount rollback. Changing column defaults in Preferences
  invalidates older saved tab layouts after restart so the Preferences controls
  continue to work rather than being silently overridden.
- Tab dragging already exists and is unchanged. The user had simply missed it.

The UI regression exercises actual header mouse events and the checklist,
field alignment and widths, search rows arriving after a layout change,
saved/restored tab layouts, and changed Preferences defaults. Integration tests
cover PATH alternatives, conflicts, ownership and helper discovery. Privileged
helper creation/removal checks run only inside an ephemeral network-isolated
Docker container, with the repository mounted read-only; they do not write
shared host directories or request a real password. Actual desktop authorization
interaction still needs real-machine acceptance. No commit/push/publication
was requested, and no new portable delivery is claimed by this section.

### 3.25 Local Beta 1 portable test build: 29 October

The user requested a tar.gz test build served through the Codespace, with its
download port opened. Built `Swordfish-Portable-0.0.3-beta1` from the current
uncommitted source, including section 3.24, using Debian 10/glibc 2.28.
The legacy Docker builder avoids the previously damaged BuildKit cache.

The actual portable binary reports 0.0.3-beta1. Xvfb launch checks passed for
the new-profile welcome, resizing, graceful shutdown, settings persistence
and no repeated welcome on the next launch. The package includes the offline
manual and non-setuid command-link helper. Fresh smoke-test portable-data is
removed before the final archive. Matching local source and SHA256 checksums
accompany the test build. Native UI/integration/device regression results
remain recorded above; real desktop authorization and user acceptance remain
pending. This is a local test delivery, not a published release or a commit.
Stable 0.0.2 and previous downloaded test archives are unchanged.

After the user confirmed downloading Beta 1, the port-8765 server was stopped.
At the user's low-disk-space request, expanded local test bundles, obsolete
Test 1/Test 2 archives, native compilation outputs and the autotools cache were
removed. The current Beta 1 binary/source/checksums, Test 3 rollback archive,
manual/UI references, user attachments and source/backups were retained.
Workspace free space increased from 2.8 GB to 3.9 GB; build/ shrank from 1.3 GB
to 173 MB. Future build scratch/staging should use the separate /tmp filesystem
where practical (38 GB free at cleanup), retaining only final deliverables in
the workspace. Docker data was left alone because it may be shared.

### 3.26 Tabs/layout not saved after changing visible columns: 29 October

The user reported that after changing columns through a heading's right-click
menu, reopening returned to one pane and a Home tab. The menu correctly changes
only the active tab; Preferences retains its separate default choices.

Reproduced the delivered Beta 1 shutdown failure:
`FileList::getHeaderSize: index out of range.` The legacy configuration saver
checked global default visibility before reading each pane's current column
width. A hidden column therefore returned index -1, causing process exit before
tabs, panel layout and the rest of the session were saved. This is an interrupted
save, not evidence that the settings file itself became corrupt.

The local fix shares width-saving logic across left, right and search panes,
checks actual column presence and preserves stored widths for absent columns.
Search Folder Name width is located by column identity, no longer fixed at
position 1. Added a regression invoking the real configuration saver after
column toggles/reordering in both panes and search, then launching a separate
process to verify both panes, tab paths/order/selection, views and column
layouts/widths restore. The full native build and expanded UI regression pass,
including that separate-process save/restore test. Validation builds use /tmp to avoid filling workspace
storage. This source fix has not yet been delivered as a replacement portable
archive; the original Beta 1 download remains unchanged.

### 3.27 Independent pane column preferences: 29 October

The user confirmed that visibility/order should be shared by all tabs in a
pane, with left/top and right/bottom independent. This replaces the earlier
per-tab visibility/order design; widths remain per-tab.

Preferences → File List now selects Left / Top Panel or Right / Bottom Panel,
initially selecting the active pane. It reads the live headings when opened,
retains pending edits while switching its selector, applies both pane choices
immediately on OK, and discards edits on Cancel. Heading-menu choices and
heading dragging affect the same pane-wide settings. Changing visibility in
Preferences preserves the relative order of columns that remain visible.

Each pane stores its stable column IDs under its own `column_layout` setting,
independently of tab restoration. Older Beta 1 settings migrate from that
pane's previously active tab. Tab changes and rollback/session restoration
no longer override pane column choices. Hidden column widths remain in tab
state and are restored from saved width entries. Sorting refreshes its field
indices from the pane being sorted, preventing the other pane's different
layout from changing the meaning of a field.

The native build and focused GUI suite pass, including actual Preferences
Cancel/OK, independent layouts/sorting, and real shutdown plus fresh-process
session restoration. The device/folder suite passed on retry without changing
assertions; its first run missed an expected simulated-unmount error message.
Build and test scratch remain on /tmp, not the workspace filesystem. Manual,
portable notes and changelog describe the new scope. No replacement portable
archive, commit, push or release has been made.

### 3.28 Test 2 delivered and confirmed working: 29 October

Built a replacement local portable test with the shutdown/session-saving fix
and independent per-pane column preferences. Build scratch and staging used
/tmp; the portable baseline remains Debian 10 / glibc 2.28. Actual portable
first-run welcome, resize, graceful shutdown, saved settings and reopening
without another welcome passed. Removed the smoke-test profile before packing.

Retained downloads in `build/beta1-test2-download/`:

- `Swordfish-Portable-0.0.3-beta1-test2.tar.gz` (80,179,793 bytes).
- `Swordfish-FM-0.0.3-beta1-test2-source.tar.gz`, matching current working-tree
  source rather than committed HEAD.
- `Swordfish-0.0.3-beta1-test2-SHA256SUMS`, verified against both archives.

Portable SHA256:
`6f03db4ee336ed2ee35fd184a836905e1e0ad46f3a107ade4f173afdb9776f09`.
Source SHA256:
`c175b136e48089dc709c92ac5557444b72c1aaa16f9d4878afc39ef918ba9e27`.

Served on port 8765 through the existing authenticated Codespaces endpoint.
The user downloaded it and reported **"tested, working"**, with further testing
to follow. At the user's request the download server was stopped; verified no
listener remains on port 8765. Temporary expanded bundles, native validation
build and smoke profile were cleaned. Retained downloadable archives,
checksums, diagnostic screenshots/logs, source and existing recovery backups.
The stable published release is unchanged; nothing was committed or pushed.

### 3.29 Cleanup and custom-action design approved: 6 November

At the user's low-disk-space request, removed three obsolete Copilot package
versions (retaining the two newest) and specifically identified obsolete
Swordfish Docker images/build-cache records. Kept the current portable builder,
all project downloads, source, notes, backups and chat history. Free workspace
space rose from 3.2 GB to 5.9 GB; the worktree status was unchanged.

The user approved implementing custom actions before the main toolbar editor:

- Thunar-style custom actions, with name, description, icon, command and
  selection/file-type conditions.
- Add Action, Add Folder and Add Separator. Folders support nested cascading
  groups rather than a flat list of actions.
- A Show on custom toolbar checkbox for actions and folders. Toolbar folders
  open dropdown menus; separators organize the toolbar.
- Keep the custom-actions toolbar separate from all existing main toolbars.
- Design shared toolbar-editing mechanisms where useful, but implement the
  main toolbar's built-in-command editor in a separate later step. Built-in
  commands must use their native handlers, not shell-command imitations.

Implementation was authorized with "agreed, go ahead". This does not authorize
a commit, push, release, or replacement of the user's downloaded Test 2.
Implemented locally: Tools → Custom Actions… opens the hierarchy editor;
Tools → Show Custom Toolbar toggles a separate toolbar with its own Edit
button. Actions support file/folder/count/glob conditions, automatically quoted
path/name/URI/current/other-pane placeholders, confirmation and terminal
execution. Context menus hide unavailable actions; toolbar buttons disable
them. Folder dropdowns expose descendants without duplicate top-level buttons.
Editor Apply changes its working copy; Save persists and Cancel discards.
Configuration uses Swordfish's own registry, with schema/hierarchy validation.
Limits are 256 entries and 16 nesting levels. PNG icons currently retain their
intrinsic dimensions; use small images. Complex shell syntax should be placed
in external scripts. Action shortcuts and main-toolbar editing are not shipped.

Full native build and install dry-run passed, as did five core and nine GUI
custom-action regression groups. Existing Beta 1 GUI regressions also passed,
including real shutdown and fresh-process tab/layout restoration. Core tests
execute hostile filenames through /bin/sh and verify quoting, conditions,
hierarchy operations and file-backed persistence. Terminal behavior was tested
with a fixture, not every terminal emulator. Existing global-window teardown
is excluded by the widget fixture; custom-controller teardown was verified.
Validation scratch/logs were moved out of the workspace to
`/tmp/swordfish-custom-actions-validation-20261106`. Manual, portable notes and
changelog updated. No new portable package or real-machine acceptance yet.

### 3.30 Custom-actions Test 3 delivery: 6 November

At the user's request, built `Swordfish-Portable-0.0.3-beta1-test3.tar.gz`
from an isolated snapshot of current uncommitted sources. Debian 10/glibc 2.28
baseline; packaged application reports 0.0.3-beta1. Actual portable first-run,
resize, graceful shutdown, settings persistence and reopening checks pass.
The new custom-action core/GUI and existing Beta 1 regression results are
recorded in section 3.29. Real-machine custom-action acceptance remains pending.

Final downloads are in `build/beta1-test3-download/`, with matching source
and verified SHA256SUMS. Portable archive: 80,353,352 bytes, SHA256
`03ea494c1294b16339efe42b1ea2576ebf4dfbcd15732837924f706a5686043d`.
Source SHA256:
`1dbcc47077ff44c3c3315fc3efa3c7b8a46111e0bf1377520c395d5caa7171b5`.
Smoke-profile data was removed before packing. Previous Test 2 is preserved.

Served only this download directory on port 8765 through the authenticated
Codespaces endpoint. Local HTTP archive response verified as 200; forwarded
endpoint responds with the sign-in redirect. Server handle:
`beta1-test3-download-server`. Stop it after download confirmation.
Temporary expanded portable/source staging and smoke profile were cleaned;
build logs retained on /tmp. No commit, push or release was made.

### 3.31 Test 3 feedback: custom icons and successful-launch output

The user's screenshots showed action/folder PNG icons expanding the toolbar
to the image's original dimensions, and a successful Firefox launch leaving
a command-output dialog. User confirmed the desired behavior: successful
normal launches without a window, failures with captured output, plus optional
Show output and Run in terminal controls.

Custom icons now fit the standard small toolbar icon height, preserving aspect
ratio. Added a persisted Show command output option, unchecked by default and
backward-compatible with existing action settings. Hidden command windows
collect output, delete themselves on successful completion, and show on failure.
Command child status is forwarded by the application harvester so consuming
SIGCHLD first does not misclassify success. Companion command-window consumers
retain their normal visible behavior and forward child status too.

Native clean rebuild and custom-action core/GUI tests pass, including a
2048-by-1024 action/folder PNG, silent success, explicit successful output,
visible failure and settings compatibility. Existing Beta 1 GUI regression
also passes, including shutdown and fresh-process session restore. Source and
manual changes are local; the already-downloaded Test 3 remains unchanged.
No replacement package, commit or release yet.

### 3.32 Custom-action navigation and Thunar-style layout agreement

The user supplied `build/thunar-custom-1.png`, `thunar-custom-2.png` and
`thunar-custom-3.png` as arrangement/workflow references and approved:

- A top-level Custom Actions menu listing configured actions/groups and Edit.
- Custom toolbar Edit separately at the far right; right-click toolbar offers
  editing. File/background Custom Actions submenus also include Edit.
- A Preferences Custom Actions tab, using the same editor/model.
- A large action/group list with compact side controls and action properties
  split into Basic and Appearance Conditions pages. Preserve nested folders,
  separators, icon selection and Show on custom toolbar for actions/folders.
- Make Show without selection prominent. Folders also enables actions on the
  current folder when no selection exists.
- One File Pattern field, no separate extensions field. `.jpg;.png;.tar.gz`
  means `*.jpg;*.png;*.tar.gz`. Explicit wildcard patterns remain supported.
  Range stays `*` (unrestricted); retain Show without selection.

Parent implemented and verified core shorthand matching, independent folder
eligibility and safely quoted current-folder placeholder expansion. Explicit
dotfile globs such as `.*` retain their meaning. Custom-action UI/navigation
integration is complete locally: top-level menu retains all configured actions,
disabling unavailable ones; context submenus always include Edit, including an
empty configuration. The separate toolbar anchors Edit at its far-right edge
and offers a right-click editing menu. Preferences opens the same editor.
Properties use Basic/Appearance Conditions tabs with command browsing, bounded
icon preview, placeholder help, prominent Show without selection, and preserved
Show on custom toolbar/output controls. Main editor retains the nested tree
with icon/name/description rows and compact side controls.

Clean native build and existing Beta 1 GUI suite passed. Six core regression
groups and thirteen custom-action GUI groups passed, including measured
far-right Edit geometry, menu availability, empty-configuration editing,
Preferences access, tabs, icon bounds, silent success and visible failure.
An initial toolbar alignment assertion caught a real positioning error; fixed
layout rather than weakening the assertion. Validation remains under /tmp.
Manual, portable notes and changelog updated. Downloaded Test 3 is unchanged.
No new package, commit, push or release is authorized by the design agreement.

### 3.33 Program/parameters and parameter legend agreement

The user supplied `build/caja-command.png` and `build/caja-legend.png` and
approved borrowing their command-entry features without replacing the agreed
Thunar-style arrangement:

- Separate Program and Parameters. Program accepts one executable name found
  on host PATH (`bash`, `thunar`, `mono`, `wine`) or a full executable path,
  typed or selected with Browse. Arguments go in Parameters.
- Working directory with Browse, defaulting to the active folder.
- Live command preview, without executing anything.
- Scrollable Parameter Legend.
- Additional placeholders for selection count, basename without extension,
  and extension, while retaining existing Swordfish placeholder meanings.

Existing full-command actions must remain compatible rather than being
silently split or reinterpreted. Paths with spaces must work safely.
Implemented locally. New actions use Program mode; existing commands retain
their original full-command mode without eager splitting. Optional fields
persist compatibly with older settings. Working directory accepts %p/%o/%d/%f
and %% as literal-path substitutions, defaults to the active directory, and
rejects missing/inaccessible directories. Programs resolve against host PATH
or a literal executable path. Preview never executes.

Added %c (actual selection count), %w/%W (basename without last extension),
%x/%X (last extension without dot). archive.tar.gz becomes archive.tar / gz;
.hidden has no extension. Empty extensions remain empty arguments; background
count remains zero even with current-folder fallback. Legend explains these.

User additionally approved Test Action: it shares preparation validation and
runs /bin/sh -n for syntax only, never executing the action. Reports Checks
Passed or clear errors; checks executable lookup in Program mode, placeholders
and directory validity. Explicitly does not promise application-option validity
or runtime success; legacy commands receive syntax checking, not arbitrary
shell-program resolution.

Native build, nine core regression groups, fourteen GUI groups and existing
Beta 1 session/UI regressions passed. Coverage includes legacy persistence,
hostile executable/cwd filenames, new placeholder quoting/edge cases, real
Program/Parameters/Browse fields, preview, scrollable legend and Test Action
success/failure dialogs. Side-effect marker tests prove checks do not run the
action. Validation remains under /tmp. Manual/portable notes/changelog updated.
No replacement package, commit, push or release requested.

### 3.34 System icons for custom actions: approved

The user supplied `build/icon choice.png` and `build/icon choice-2.png`:
custom-action icons should be selectable from installed system icons as well
as image files. Agreed implementation adds a searchable visual chooser with
system icons, Image Files and No Icon routes, preserving bounded toolbar
sizes. Portable resolution must use host icon data rather than only bundled
Swordfish assets.

Implemented locally. The chooser loads forty previews per page and supports
search, paging, Cancel, image selection and resetting to the default icon.
System selections persist `theme:NAME` and resolve host XDG themes, inheritance
and hicolor fallback; image selections persist absolute paths. PNG/JPEG/XPM
load natively; SVG uses the existing converter and cache. All sizes preserve
aspect ratio. Missing selected icons report an error.

Native build, sixteen custom-action GUI groups, nine core groups and existing
Beta 1 GUI/session regressions passed. Coverage includes synthetic host themes,
inherited/fallback names, portable host-vs-bundle lookup, image formats/bounds,
search/paging, cancellation and reset. Initial search-fixture failure targeted
the category combo's internal field rather than Search; corrected fixture
target selection while retaining the zero-results assertion. Manual, portable
notes and changelog updated. No replacement package or release yet.

### 3.35 Small editor improvements approved

The user approved prioritizing Duplicate Action, suggesting an installed
application's icon, and unsaved-change protection before another test build.
Implemented locally. Duplicate Action clones every field, assigns a new ID,
retains the parent, inserts beside its source and selects the copy.
Use Program Icon explicitly suggests an installed application's icon rather
than silently overwriting the user's choice. Editor and properties compare
meaningful fields and protect dirty Cancel/Escape/window-close routes with
Discard / Keep editing; unchanged dialogs close without a warning.
Native build, ten core and seventeen custom-action GUI regression groups,
and existing Beta 1 GUI/session regressions passed. Covers duplicate
selection/persistence, suggestion/overrides and dirty-close routes. Manual,
portable notes and changelog updated. No replacement package yet.
Import/export, action keyboard shortcuts and toolbar
icon-size/text options are deferred, not part of this implementation.

### 3.36 Custom-actions Test 4 delivery: 6 November

User requested a test build and download port. Built from an isolated snapshot
of current sources on /tmp, including sections 3.31-3.35. Debian 10/glibc 2.28
baseline; version remains 0.0.3-beta1. Actual packaged first-run welcome,
resize, graceful exit, settings persistence and restart smoke passed.
Removed portable-data before final packaging; archive exclusion verified.

Retained downloads: `build/beta1-test4-download/`, with matching source and
verified SHA256SUMS. Portable:
`Swordfish-Portable-0.0.3-beta1-test4.tar.gz`, 80,388,098 bytes.
SHA256 `11476c00223d9ed13cd6db41c12765434c35f00a90cfc1886077e174fd1c1d40`.
Matching source:
`Swordfish-FM-0.0.3-beta1-test4-source.tar.gz`.
SHA256 `31b3d9b1dcc2d2a7a376d57f4f9d470e4a65181a120ad465beed0abad4cc01d9`.

Stopped the Test 3 server and served only the Test 4 directory on port 8765,
handle `beta1-test4-download-server`. Local archive returned HTTP 200; forwarded
Codespaces endpoint responded with its sign-in redirect. Stop the server after
download confirmation. Previous test archives are preserved; temporary expanded
Test 4 source/bundle and smoke profile cleaned, build log retained on /tmp.
Real-machine testing remains pending. No commit, push or release.

### 3.37 Test 4 command-entry feedback and simplified modes

The user reported disabled/uneditable command fields and too many competing
entry boxes in `build/custom-action-config.png`. Cause: a legacy action showed
its editable full command alongside disabled Program/Parameters fields.
Agreed Caja-style simplification: show only Program/Parameters normally;
Advanced: full shell command replaces them with one editable Command field.
Working directory and preview stay visible. Preserve saved legacy command
bytes and mode; do not silently split shell commands.

Implemented locally, with reduced field minimum widths. Native build and all
seventeen action GUI groups pass; added explicit visibility assertions for
both modes alongside existing compatibility/editability checks. Manual and
portable notes updated. Downloaded Test 4 remains unchanged; no new archive.

### 3.38 Built-in toolbar editor and image-file previews

The user selected the previously agreed main-toolbar editor as the next item,
then requested previews for images in the custom-action icon chooser.

Added **View → Customize Main Toolbars…** for the General, Tools and Panel
toolbars. Each editor keeps its built-in commands on their original native
FOX targets/selectors and supports add/remove, reordering, separators and
restoring defaults. Configuration is stored separately from Custom Actions;
address, tab and custom-action toolbars are not included. Saved entries are
validated against the available command catalog before use.

The custom-action **Image Files** route now browses a selected folder, shows
image thumbnails and a larger preview for the selected image, and retains the
existing image validation and aspect-ratio handling.

Native build and all eighteen custom-action GUI regression groups passed.
Coverage includes a preconfigured startup toolbar, native target preservation,
editing/persistence, canceled and applied Restore Defaults, image thumbnails,
large preview and cancellation. Manual, portable notes and changelog updated.
No replacement package, commit or push requested.

### 3.39 Test 8 acceptance, toolbar rollback and 0.0.3 beta 1: 6 November

Tests 5-7 exposed toolbar regressions on the user's machine despite local
regressions passing: missing right-click menus, excess separators, ineffective
overflow controls and missing zoom access. Attempts to repair the presentation
layer were rejected in favor of restoring the working built-in toolbars.
Section 3.38 is historical: its built-in toolbar editor is no longer present.

The final rollback removes the toolbar editor, right-click presentation menus,
icon-size/display preferences, overflow controls, separator parking and special
toolbar mouse routing/application-dispatch workaround. Native General, Tools and
Panel toolbar commands and separators were compared directly with the original
source and match. Old customization registry entries are ignored, not migrated
into the retained interface. File-panel zoom remains; no toolbar icon-size
preference remains.

Retained and validated:

- Custom Actions creation/editing/duplication, folders/separators, conditions,
  placeholders, save/reload, confirmation, terminal and host-safe execution.
- Top-level Custom Actions, file/background contextual actions, Tools and
  Preferences access, plus the separate labeled custom toolbar and rightmost
  main Settings icon with the "Edit Custom Actions" tooltip.
- Maximizable system-icon chooser with a responsive labeled grid, categories
  from freedesktop Context metadata or standard directory names, and search.
- Image Files opens the normal file chooser with a bounded live preview below
  the left sidebar. Accept closes both choosers and applies the icon directly
  to Properties; cancel preserves the previous choice. No extra preview window.
- The manual, integration options, column preferences and other Beta 1 fixes
  documented in the changelog.

Test 8 was built on Debian 10/glibc 2.28. Model regressions, full local build,
GUI regressions with/without Fluxbox, real pointer image selection, actual
maximization, invalid-image errors, settings persistence and clean teardown
passed. The GUI suite also passed against Debian-built objects and packaged
theme icons. The actual portable launcher passed first-run, resize, clean exit
and restart checks. On 6 November the user reported "seems OK" and authorized
publication, then requested documentation first, commit/push/release second,
and safe disk cleanup last.

Release tag: `swordfish-0.0.3-beta1`; title: **Swordfish 0.0.3 beta 1**.
The user explicitly chose GitHub's Latest badge while retaining beta labeling;
GitHub cannot mark a prerelease as Latest, so this release is not marked with
the platform's prerelease flag. This is not a claim of final 0.0.3 stability.
The stable 0.0.2 release/tag/assets must remain unchanged.

Publish matching portable ZIP/tar.gz, source archive, offline-manual ZIP and
SHA256SUMS. Exclude portable-data, test profiles and build logs. Preserve manual
illustrations in source and packages; only obsolete test screenshots/downloads
and known temporary build products are cleanup candidates. Never use a broad
git reset/clean to perform this cleanup.

### 3.40 Beta 1 startup delay fixed locally: 7 November

The user reported that published 0.0.3 beta 1 started slowly. Their MX Linux
measurements (`build/measure-startup.py`, alternating 3-5 runs against Xfe)
isolated the delay to Custom Actions using `theme:` icons in `xferc`. Removing
only those icon lines restored normal startup. The cause was startup icon
resolution building the complete system-icon chooser catalog by scanning every
installed theme.

Fix, committed locally as `a62bc2e` on top of `64dfde3` (not pushed):
configured theme icon names are batched and resolved through a targeted,
cached lookup. The full catalog is built only when the icon chooser opens.
Theme precedence, raster/SVG/size selection and pixmaps fallback are preserved
and covered by expanded GUI regressions. Native build plus both Custom Actions
regression suites passed before and after the commit; the committed source
matched the delivered test build byte-for-byte.

The user's matched run on 7 November, using separate temporary copies of the
same `xferc` with four theme icons (medians: main window / Ctrl+F search ready):

| Build | Swordfish | Xfe in the same run |
|---|---:|---:|
| Published beta 1 | 4.07 s / 4.56 s | 0.62 s / 1.08 s |
| Icon-startup test build | 0.90 s / 1.51 s | 0.63 s / 1.08 s |

The user also confirmed the four icons, launching an action and the folder,
opening the editor and the icon chooser all work in the test build.

Test build: `Swordfish-0.0.3-beta1-icon-startup-test.tar.gz`, SHA256
`39bc69016ab617c214d2d94e7585a4ad6a81340773eab639a5d013e2fe6d2390`;
source SHA256
`34ecad8521c91c602082fa64324c68777c60661bd88bb81ac9ee2a9fed0b4621`.
Both remain in `build/` with their SHA256SUMS file. The user installed the
test build at `~/Applications/Swordfish-0.0.3-beta1-icon-startup-test` and will
retest it later. If it passes, work resumes on features and modifications.
Pushing or publishing the fix still requires explicit permission. The
published beta 1 and stable 0.0.2 releases are unchanged.

The user later retested this build and reported it working.

### 3.41 Custom Action Properties simplified to one Command field (uncommitted)

The user asked for the full shell command to be the only command-entry method.
Changes in `src/CustomActionsUI.cpp`:

- Program, Parameters, the **Advanced: full shell command** checkbox and the
  live command preview were removed. One **Command** field remains; its
  Browse inserts a quoted program/script path (with `%` escaped as `%%`).
- **Show on custom toolbar** moved to a **Toolbar** group at the top of the
  Appearance Conditions tab for actions. Folders have no Appearance tab, so
  their checkbox stays on Basic.
- Help labels, the Parameter Legend, Test Action messages and the tooltip were
  reworded for the single command. New actions use command mode
  (`programMode = false`).
- Compatibility: the data model is unchanged. Program-mode actions saved by
  earlier test builds open as one equivalent command via the new
  `CustomActions::commandText()` (quotes unusual program paths, doubles `%`).
  If the text is not edited, the action is kept exactly as stored, so no
  false unsaved-changes prompt appears. Edited text is saved as a normal
  command, clearing Program/Parameters. Execution of program mode is unchanged.
- Layout follow-up (user review of screenshots): the placeholder list has a
  fixed width with Insert beside it; Parameter Legend and Test Action moved to
  their own line below. The dialog can be maximized. The user chose to keep
  the text fields at their fixed width rather than stretching with the window.

Tests: the core suite now checks that a converted command (path with spaces, a
quote and `%`) produces identical output. The GUI suite checks the new field
layout, no preview/mode switch, the toolbar checkbox location, and that a
program-mode action opens as one command and closes without a prompt.
`make -C src`, `tests/run-custom-actions-regression.sh` and
`SWORDFISH_TEST_WM=1 tests/run-custom-actions-ui-regression.sh` all passed.
`portable/README.md`, `docs/manual/index.html` and `CHANGELOG.md` were updated.
Not committed, packaged or pushed.

### 3.42 Unmounted Devices labels use muted text (uncommitted)

The user clarified the first item in `build/for discussion.txt`: connected
unmounted USB drives and internal partitions, and disconnected network shares,
should be visibly muted until mounted/reconnected. A safely ejected USB drive
should disappear once the system removes it; ejecting a disconnected share
forgets its Devices entry.

`src/Places.cpp` now paints enabled unmounted device labels with a muted color
derived from the active theme's text and background, including the selected
colors. It preserves FOX's icon, label, focus and selection geometry. The
color keeps at least 4.5:1 contrast when possible; if further muting cannot
maintain that contrast, the theme's original foreground is retained. The
existing `(unmounted)` suffix still identifies the state in that case. Mounted
labels, actions, mount/eject controls and enabled/clickable behavior are
unchanged. Connected unmounted devices remain available to mount; remembered
GVFS shares remain reconnectable for the current session.

Validation: `make -C src -j2` passed. The focused
`tests/run-folder-expansion-regression.sh` passed with a staged icon prefix:
it checks light/dark and selected colors, contrast and FOX drawing geometry;
simulates USB/internal mount-unmount transitions and USB ejection; and checks
share disconnect, reconnect and eject. Its host `lsblk` query is read-only;
device and share actions in the regression use fixtures. No physical hardware
or real network share was tested. The user has not tested this change yet.
The portable README, manual and changelog were updated. No test package,
commit, push or release was made.

### 3.43 Flat play/pause/eject device icons (uncommitted)

The user asked for video-player-style device controls: play for Mount, pause
for Unmount (the mount is "still there ready to resume") and eject for Eject.
They must be flat line drawings, black on light backgrounds and white on dark
ones, everywhere except Close Archive Folder.

The reference sites could not be fetched, so the symbols are original
geometry defined once in `src/icons.cpp` (`deviceSymbolOutlines`).
`drawDeviceSymbol` draws them in sidebar device rows (`src/Places.cpp`),
replacing the bordered green play, red stop and red eject shapes; hit areas
are unchanged. `loadAppIcons` also generates antialiased
`minidevicemounticon`, `minideviceunmounticon` and `minideviceejecticon`,
sized like `minimount.png` and blended over the theme base color. These replace
the device Mount/Unmount/Eject icons in `DirPanel.cpp`, `FilePanel.cpp` and the
Tools menu/toolbar in `XFileExplorer.cpp`. `deviceSymbolColor` chooses black
or white by higher contrast with the actual background (including selected
rows). Close Archive Folder still uses `miniunmounticon`; theme PNGs are not
changed.

Review feedback: the first preview was fuzzy (strokes off the pixel grid) and
the user wanted solid pause bars. `deviceSymbolShapes` now maps the design
symmetrically onto whole pixels (stroke 2 px at 16-20 px sizes), pause is two
filled bars, and only diagonal edges are antialiased. The user also asked to
drop the `(unmounted)` suffix when dimming works: `drawUnmountedDevice` strips
the translated suffix from the drawn label whenever the muted color differs
from the theme text; the item text itself still carries the suffix, so the
fallback for undimmable themes is unchanged.

Validation: `make -C src -j2` and the folder-expansion regression (44 PASS,
tests updated for the suffix rule) passed with a staged icon prefix. A preview
was rendered from the real generated icon pixels via a temporary harness; the
user has not tested a build yet. No test package, commit, push or release.

### 3.44 Custom toolbar icon size (uncommitted)

The user requested a Preferences-only toolbar icon size (no context menu).
A first attempt also covered the General/Tools/Panel toolbars by swapping in
enlarged copies after construction. It started crashing with heap corruption
(valgrind: invalid writes in the `XFileExplorer` constructor). This was caused
by stale objects after a header change, and a clean `make -C src clean` rebuild
fixed it. However, the user recalled that main toolbar changes were what forced
the Beta 1 rollback (3.39) and chose **custom toolbar only**. The main toolbar
code was removed; `XFileExplorer.cpp` contains only the 3.43 icon swaps.

Implementation: `SETTINGS/customtoolbariconsize` (16/24/32, read via
`xf_toolbariconsize`). `CustomActionsUI::rebuild` gives toolbar buttons
separate icons: actions with an image or `theme:` icon are reloaded from the
source at the larger size (quality depends on the source); fallback/builtin icons use
`xf_toolbaricon` (24 = smooth, 32 = exact doubling; device symbols redrawn).
Menus keep normal icons. Preferences applies the change immediately by
rebuilding the custom toolbar. Note that the Makefile does not track header
dependencies reliably, so run a clean rebuild after header edits.

Follow-up preflight found that wide icons could shrink instead of grow because
the reload size used the icon's height rather than the standard toolbar size.
The loader now uses the standard size as its bounding box. The GUI regression
was updated for the new Preferences setting and checks wide file and system
icons, normal/medium/large sizes, disk persistence, Cancel, normal menu icons,
and unchanged main-toolbar icons.

Validation: clean build; launched under Xvfb at 16/24/32 with sample actions.
The native build and Custom Actions model and GUI regressions passed. The
folder-expansion and search-window regressions also passed when linked with
`-lXrandr`, required by the current local build configuration. The user has not
tested these local changes yet.

### 3.45 Discussion-list triage before a test build

On 8 November the user had about one hour and asked which discussion items
could be completed quickly and safely. The list still contains several items
that are already implemented: per-tab column widths, the always-visible
archive-folder action for supported archives with a missing-`archivemount`
message, the menu-bar Preferences button, Copy Path, and grouped Copy/Move/Link
actions including Other Pane. The user manual already exists as bundled offline
HTML. Online publication, version-numbering policy and the meaning of
"detailed representation of ui" still need discussion.

The dimmed device labels, play/pause/eject controls, simplified Custom Action
dialog and custom-toolbar icon-size setting are local changes awaiting user
testing. The wide-icon issue found during this preflight was fixed and tested
as recorded in section 3.44. The larger freedesktop multi-size icon/theme plan
is documented in `build/for discussion.txt` and should not be rushed into this
test build. No new test package, commit, push or release was made during this
triage.

### 3.46 Beta 1 updates test build (local, uncommitted)

The user approved a separate portable test build, not a release. It includes
the Custom Action dialog, dimmed unmounted devices, play/pause/eject controls
and custom-toolbar icon sizing described in sections 3.41-3.44. The larger
freedesktop icon/theme replacement remains deferred. No commit, push, published
release or change to the user's installed Beta 1 was made for these updates.

The x86-64 bundle was built from a frozen working-tree snapshot on Debian 10
(glibc 2.28). The local download directory is
`build/beta1-updates-test-download-20261108/`; it is ignored by Git and must
not be mistaken for a permanent release. The source archive matches the build
snapshot and predates this history entry:

| Archive | SHA-256 |
|---|---|
| `Swordfish-0.0.3-beta1-updates-test.tar.gz` | `544447ef8ee9015026a7b61f5c9eba9aa6d594876e4c5ffd93058e6d225eb3d4` |
| `Swordfish-FM-0.0.3-beta1-updates-test-source.tar.gz` | `ec633cbc217be94ed0f9e0f28c3e3374b0bbd3a8d98300e5f3c8b96204509f63` |

Both archive checksums were verified. The portable archive omits
`portable-data`; on normal launch it creates its own profile in the extracted
test folder. Existing Beta 1 settings may be **copied**, not moved, into the
test profile if the user wants their Custom Actions.

Validation used the extracted archive and a separate temporary HOME under
Xvfb with a window manager: first launch, resizing, clean exit, saved settings
and restart passed. The Custom Actions GUI regression also passed with the
portable-built object files and packaged default icons. Earlier native build,
Custom Actions, device and search regressions are recorded above. The user has
not yet tested this build and said they will test it later; no physical devices
were exercised in these checks. The user normally extracts the archive with
Archive Manager and runs `AppRun`; terminal commands are not required.

After delivery, the user asked to clean disk space. The verified temporary
source snapshot, build copies and extracted portable output were removed,
along with the unused `swordfish-portable-builder:debian-buster-slim` Docker
image. The next portable build will need to rebuild that image and may require
network access. The test binary and matching source archives, checksums,
earlier test and release files, and `build/for discussion.txt` were retained.
The private download server was left running, but its link depends on the
Codespace remaining available and may need to be restarted in a later session.

Package inspection also found 18 dangling Swordfish-named theme links created
by `iconlinks.sh`; the same links and absent targets are present in the
previous icon-startup test package. The application currently loads its window
icons by other names, and the hicolor application icons are present. This
pre-existing packaging issue was not changed during the time-limited test build.

### 3.47 Appearance Preferences usability feedback (pending)

The user initially could not find the toolbar icon-size setting. After its
location was explained, they supplied `build/appearance.png`, showing the
Appearance page with Large (2x) selected. This confirms initial UI inspection,
not successful testing of icon sizing or physical-device behavior.

The user asked to make a note, not implement another build: rename the section
to **Tool Bar Icon Size** and replace the dropdown with three clearly labeled
choices, **16**, **24**, and **32** pixels. Mutually exclusive radio buttons
are the suitable form of the requested radio-button/tick-box choice; keep the
Custom Actions toolbar scope explicit and leave main-toolbar sizing deferred.

The Appearance page construction also needs review. The screenshot shows
stretched controls and a partly obscured lower row in Custom Colors. Review
grouping, widths and resizing so controls remain visible and the size setting
is easy to find; the final page layout still needs discussion. This follow-up
is recorded in `build/for discussion.txt`. The screenshot and discussion file
are local, Git-ignored resources. No application code, test archive, commit,
push or release was changed for this note.

### 3.48 Nested portable launch breaks host networking (local fix)

The user reported that the updates test discovered only their own computer
and that unmounting an existing SMB share failed with "Containing mount does
not exist" (`build/error-unmount.png`). The earlier icon-startup test still
worked. They had launched the new test's `AppRun` from the older Swordfish.
Closing the test and starting it from Thunar restored operation, as confirmed
by the user. Switching file managers is a workaround, not the required UX.
The user also confirmed that launching through a Custom Action in the older
build works. Custom Actions already wraps execution with `xf_hostcommand`;
that clean launch route explains the difference from directly running the file.

Discovery, unmount code and build-helper logic are unchanged from Beta 1.
The two delivered test archives also contain identical launchers. The launcher
unconditionally overwrote `SWORDFISH_HOST_*`, recording the older portable
process's private PATH, libraries, XDG paths and GIO modules as the desktop
environment. Later host commands restored that contaminated environment.

`portable/AppRun` now preserves already-recorded original host settings,
including explicitly empty library/module values. First-launch capture,
separate portable profiles, preview-loader setup and companion launchers are
unchanged. The new `tests/portable-launcher-regression.py` reproduced the
nested-launch failures before the fix; it covers defaults, custom host paths,
inherited portable settings, empty host values, two-launcher chaining and
AppImage/companion profiles. No C++ or device-operation changes are required.
All six launcher regressions pass. A fixture using the actual older archived
`AppRun` also passes when chained into the corrected launcher.

The corrected package is `Swordfish-0.0.3-beta1-updates-test2`, stored in
`build/beta1-updates-test2-download-20261108/`. It reuses the verified updates
test's binaries and libraries, changing only `AppRun` and the root README;
checksums verified all 9,450 other regular runtime files were unchanged.
The matching source archive retains the original application sources and adds
the corrected launcher, its regression tests and updated documentation.

| Archive | SHA-256 |
|---|---|
| `Swordfish-0.0.3-beta1-updates-test2.tar.gz` | `37c25fa7927635a99b13035750b4e3a2cf8b20a08a43685083f69249bd4c8785` |
| `Swordfish-FM-0.0.3-beta1-updates-test2-source.tar.gz` | `96e36c52b6d5728c562a517982b2d540b813a994e9278366a88d6d56ebe44bc9` |

The extracted corrected archive passed first launch, saved settings, resizing,
clean shutdown and restart under Xvfb, both directly and through the actual
older archived launcher with a fixture application forwarding to the new app.
The real running process preserved the original host PATH, library path and
GIO module settings. The matching source archive predates this delivery entry.
The corrected archives and checksums are served separately on private forwarded
port 8766; the earlier download on 8765 was not replaced. Temporary extraction,
fixture and packaging copies were cleaned up after verification.

The user tested the corrected build and confirmed it is working. This confirms
the reported network discovery and share-unmount regression is resolved on
their setup; it does not validate unrelated physical-device operations. The
Appearance-page/radio-button request in section 3.47 remains pending.
No commit, push or published release was requested.

### 3.49 Framed device control icons (uncommitted)

After testing, the user asked for two changes to the flat device symbols from
section 3.43, to do next time:

- Draw the Mount (play) and Unmount (pause) symbols inside a square or circle.
- Fix the Eject symbol: it looks distorted. The user preferred the earlier eject
  design from before the pixel-grid adjustment in section 3.43.

The symbols are drawn in `src/icons.cpp` and `src/Places.cpp`. Check them at
real size on light and dark themes before the next test build.

The user supplied comparison screenshots `build/icons.png` (current flat black
symbols without frames) and `build/old icons.png` (earlier framed square
buttons with coloured symbols: red dot, green play, red eject with base bar).
The framed buttons and old eject shape are the preferred direction.
The user then rejected the red dot (ugly) and fixed red/green colours (they do
not work on all backgrounds). Plan: square frames and the old eject shape, with
play/pause/eject symbols and frames drawn in one contrast-chosen colour.

Implemented locally: `deviceSymbolShapes` now draws a thin square frame plus a
solid play triangle, two pause bars, or a solid eject triangle over a bar, all
pixel-aligned. Sidebar rows (`src/Places.cpp`) now use the same smoothed
`makeDeviceIcon` rendering through a per-background cache instead of separate
line drawing; `deviceSymbolColor` is internal. Validation: `make -C src`
without warnings; folder-expansion regression 44 PASS (needs `-lXrandr` locally);
preview `build/v3-icons-preview.png` rendered from the real code at 16/20/24/32
px on white, grey and dark backgrounds. No test build, commit or release.

User note: play/pause/eject exist in most freedesktop themes as
`media-playback-start`, `media-playback-pause` and `media-eject` (present in
Adwaita and Humanity, not in the bundled sets). Stage 3 should map to them when
a theme provides them, keeping the drawn symbols as fallback.

Further user notes for Stage 3 and later: most themes also provide device and
network-share icons (`drive-harddisk`, `drive-removable-media`, `media-optical`,
`computer`, `network-server`, `network-workgroup`, `folder-remote`), so sidebar
rows should map to them with bundled fallbacks. A future build could extend
network discovery to FTP, SSH/SFTP, media servers, WebDAV and NFS, each with a
distinct theme icon. Recorded only; not designed or implemented.

Test 2 feedback (8-9 Nov): the user confirmed network-share mount, unmount and
eject work. USB was initially unavailable; on 9 November the user reported that
the USB test passed.

Unmounting an internal partition asked for an administrator password
(`build/pw.png`: "mounted by another user"); `build/umerror.png` shows
`NotAuthorizedDismissed` after the prompt was cancelled. The user first reported
that Thunar and Caja did not ask, then corrected this: both also ask when
unmounting the same partition. Swordfish uses host `udisksctl unmount -b`, with
authorization handled by UDisks/Polkit. No code or system policy was changed.

### 3.50 Pre-modularization baseline and additional requests (discussion)

The user wants a stable, tested build that can be committed and pushed before
starting a modularization branch. Current source is not identical to updates
test 2: comparison of regular source/test/portable files found 137 matching
files and three changed icon-related files (`src/icons.cpp`, `src/icons.h`,
`src/Places.cpp`). The framed icon revision still needs testing in a portable
build. Documentation has also changed; generated build and translation changes
must be reviewed separately rather than committed indiscriminately.

The user proposed six further items on 9 November, not yet implemented:

- Put transparent PNG, JPG and alternative application icons in an `app-icon`
  folder inside the main folder. Confirm whether this means the extracted
  portable root beside `AppRun`. Existing folder and circular artwork is
  available; JPG cannot preserve transparency.
- Add three or four smaller zoom-out levels because increased DPI makes icons
  too large. Confirm which icon surfaces should change. Current file zoom has
  five steps and affects Details icons as well as Icons/Thumbnails; preserve
  existing saved zoom choices when extending it.
- Make the default address bar editable text with dropdown history. Current
  pane bars have a separate history arrow, breadcrumbs/read-only text and a
  temporary text editor. Clarify new-profile defaults versus existing choices.
- Make the root-launch toolbar icon red and root-window title bar/borders red.
  Native title-bar/border colours are generally window-manager controlled;
  an application-owned red warning/border is an alternative, not yet approved.
- Add Batch Rename to Tools, accepting a selected folder or multiple files.
  Reuse the existing `RenameDialog`; clarify whether selecting a folder means
  its contents or renaming the folder itself.
- Show the dragged column/tab and its drop position, like Firefox. Existing
  column and tab reordering lacks that feedback; preserve resizing, tab state,
  file dropping and hover-to-switch behaviour.

These are requests for scope discussion, not all purely cosmetic changes.

Implementation (9 November, uncommitted, not yet user-tested). The user chose
"do any items that can be done safely":

- `app-icon/`: folder and circle artwork as transparent 256/512 PNGs and white
  512 JPGs, plus 256/512 shield JPGs from `logo.jpg`. Installed to
  `share/swordfish/app-icon`; the portable root gets an `app-icon` symlink.
- Zoom: three extra zoom-out steps (icons 24/20/16, Details rows 14/12/10).
  Saved steps 0-4 remain valid.
- Address bar: text mode is the default; local folders use a permanently
  editable path field. The existing history arrow beside Back is the dropdown.
  Fixed Preferences reading/writing `showpathlinker` while panels used
  `show_pathlinker`.
- Root: the root toolbar icon is tinted red in every theme; a root window has a
  red menu bar with white text. Title bar/border colours stay WM-controlled.
- Tools > Bulk Rename... (reusing the existing translated label). Multiple items
  work as before; one selected folder renames its direct contents, sorted
  naturally, including hidden files only when shown. An empty folder shows an
  information message.
- Drag preview for columns/tabs: not attempted; deferred as riskier.

Validation: clean `src` build; Custom Actions regression and UI regression
(including new zoom-out test); portable launcher regression; staged
`make install`; folder-expansion regression 44/44 (run from a copy with
`-lXrandr`, a pre-existing link omission in the repository script). Xvfb
screenshots confirmed the editable path field and navigation, red root menu
bar and icon, Bulk Rename dialog listing a folder's contents, an actual rename
on disk, and the empty-folder message.

Baseline test build (9 November, user-approved): built from a frozen snapshot
on Debian 10 / glibc 2.28 as `Swordfish-0.0.3-beta1-baseline-test.tar.gz`
(SHA-256 `d63edd1e72444789ca0ca51db7527fc9d8733ce223a85f10708c908b8b8658fc`)
with matching source archive (SHA-256
`c735d20e7c7e198ca641ec64df5ec65523c1747441e29803e749f8484507e208`) in
`build/baseline-test-download-20261109/`. It also contains the untested framed
device icons. The portable first-launch smoke test passed. Awaiting user
testing; no commit, push or branch was made.

User feedback on that build: no visible history arrow and no way to switch to
breadcrumbs. Fix: the Folder History arrow moved from beside Back to the right
end of the address field, dropping its list down below the arrow (it stays
greyed out until there is history). The pencil button now toggles
`show_pathlinker` (text/breadcrumbs) for both panels, saved immediately;
Ctrl+L still focuses/edits the path. Preferences re-reads the setting each
time it opens so it cannot overwrite the toggle. Verified in Xvfb (history
list, toggle, persistence after restart) and with the Custom Actions, UI,
portable launcher and folder-expansion regressions. Rebuilt as
`Swordfish-0.0.3-beta1-baseline-test2.tar.gz` (SHA-256
`dc2eb9f9e316f6da07d68a3d763adfcb98c372da69b4e64bf1cb26e5d95f21e2`) in
`build/baseline-test2-download-20261109/`; smoke test passed.

The user confirmed the old builds' path-linker preference did not work and
found its wording ambiguous. Following their suggestion, the working-tree
preference is now labelled **Default address bar mode**, with **Text** and
**Breadcrumbs** choices. It uses the same saved setting as the address-bar
switch. Fixed the text field remaining visible alongside breadcrumbs when a
Preferences change refreshes the panes without an immediate restart.
A clean source build and the UI regression passed; isolated Xvfb checks
confirmed that each choice survives an application restart. This change is
not included in baseline test 2, and no new portable build was made.

Follow-up (9 November): the user asked to keep things simple — make the
existing history buttons clearer and the mode setting clearer. The setting is
now two radio buttons in a **Default address bar mode** group: **Button mode
(breadcrumbs)** and **Edit mode (text)** (same `show_pathlinker` key). The
pane Folder History arrow returned beside Back (its original position); it and
the top toolbar's Back/Forward history arrows now share `xf_historybutton()`:
raised, thick-framed, larger arrow scaled by `screenres`. The pane list drops
down from the arrow's left edge, clamped to the screen; the toolbar lists open
below their buttons instead of at fixed offsets. Clean `src` build; Custom
Actions regression and the GUI regression (6 consecutive runs) passed,
covering both panes, both modes, empty/populated history and selection.
Not yet in a portable build.

Trash bug (9 November, all builds): Places → Trash showed "Folder
~/.local/share/Trash/files doesn't exist", and trashed files were invisible to
Thunar/Caja (`trash:///`). Cause: portable `AppRun` redirects `XDG_DATA_HOME`
into the bundle, so every trash path built from `xdgdatahome` pointed at a
private trash inside the bundle, while Places hard-coded the real
`~/.local/share/Trash/files`, which does not exist until something is trashed.
Fix: new `xf_trashdatahome()` (prefers `SWORDFISH_HOST_XDG_DATA_HOME`, then an
absolute `XDG_DATA_HOME`, then `$HOME/.local/share`) is used for all trash
paths including Places; `DirPanel::onCmdGotoPlace` and `FilePanel::navigatePane`
create the trash `files`/`info` folders (0700) on demand. Verified under a
portable-like environment that startup creates the host trash and no bundle
trash; both regressions pass. Note: items trashed by older portable builds
remain in the bundle's private `data/Trash` and will not appear in the new view.
This fixed Home Trash only, not the combined mounted-drive view provided by
desktop file managers; see section 3.51.

Trash controls (9 November): the user found no visible way to empty the trash
(the existing Trash menu / Ctrl-Del was overlooked). Added, Caja-style, a
**Trash** bar above the file list (`FilePanel::trashbar`, toggled in
`onUpdStatus`) shown only while that panel displays the trash, with
**Restore Selected Items** (activates that panel, then restores to each item's
original location) and **Empty Trash**; plus **Empty Trash** in the sidebar
right-click menu of the Trash can place (`DirPanel::onSidebarPopup`). Trash
columns now match Caja's Name, Size, Type, Trashed On and Original Location
order. The bar's Empty button forwards via
`ID_TRASHBAR_EMPTY` because `mainWindow` is still NULL while panels are
constructed. Verified in Xvfb: bar shows/hides, Restore enables on selection
and restores, both Empty paths confirm and empty `files`/`info`.

Integration / toolbar-size follow-up: the user reported repeated launcher
creation and an error when changing toolbar size, even though integration
appeared unchecked. The screenshot showed refusal to remove a modified desktop
entry. Ownership verification failure had cleared the displayed choices, and
every Preferences Accept reapplied integration, including unrelated saves.
Recorded choices now remain visible on verification failure; only integration
controls are disabled with an explicit warning. Other preferences still save.
Integration is applied only when its choices or enabled command destination
change. Reapplying unchanged desktop/menu entries no longer calls `fchmod`
unnecessarily, preserving inode, mtime and ctime when permissions already match.
Strict refusal to overwrite or delete changed/unrelated launchers is retained.
Pure launch-only recreation has not been reproduced; no startup integration
apply call was found.

The specific request in section 3.47 is now implemented locally:
**Tool Bar Icon Size**, with **16 px**, **24 px**, **32 px** radio buttons.
This changes only the custom toolbar, retains DPI scaling, applies on Accept
and discards on Cancel. Broader Appearance-page redesign remains deferred.
A clean source build, System Integration regression and window-manager GUI
regression passed. Coverage includes radio exclusivity/layout, unchanged
launcher/state metadata across size saves, saving with a modified desktop
entry, and intentional integration removal versus Cancel.
Manual, portable notes and changelog updated. No new portable package,
commit or push; the baseline-test2 download remains unchanged.

Follow-up verification (9 November): the "launchers recreated" symptom is
caused by Preferences Accept, not startup. With inotify watching the launcher
folders, baseline test 2 produced ATTRIB/CLOSE_WRITE on both launchers and the
state file on every Accept with no changes; the current build produced none,
and three portable starts produced no events and identical checksums with
either build. Ownership is now recognised by the Swordfish marker comment or
the recorded Exec line, so a desktop-reformatted launcher keeps the choices
checked and enabled; only truly unrelated files lock the integration controls.
Unchanged launchers are opened read-only. Verified in Xvfb with the portable
bundle: modified launcher, select 32 px, Accept, no error dialog, launchers
unchanged, `customtoolbariconsize=32` saved.

Drag fixes (9 November): dragging to Chrome or a text editor failed because
Swordfish offered Move, which copy-only targets reject. When the pointer is
outside Swordfish, Move now becomes Copy (`xf_dragaction`), and the UI log
records `drag-to-other-program` accepted/rejected. The user also reported a
stop sign on clicking items in a writable MegaSync folder. Cause: a click with
slight movement starts a drag, and the cursor was chosen before the target's
XDND status reply arrived, so the first frame always showed stop and stayed
until the pointer moved again. `FXApp::dispatchEvent` in `foxhacks.cpp` now
updates the drag cursor when the status arrives. `FileList::onDragged` also
used `orgdirectory` (set only on DND enter, so empty on the first drag and
stale afterwards) for the read-only source check; it now uses the list's own
folder. Verified in Xvfb: short drags show the move cursor, a read-only target
folder still shows stop, and both regression suites pass.

The user then found that drags fail when the target is on another desktop
(FeatherPad on desktop 2, Chrome restored from the panel): the pointer becomes
normal and the file is lost, while Caja works. Cause: X releases a pointer grab
when the grab window becomes unviewable, and FOX grabs on the source widget.
`xf_keepdraggrab()` (called from `FileList`/`DirList::onBeginDrag`) moves the
active grab to a 1x1 override-redirect window at -10,-10 that is mapped with
`XMapRaised` (FOX `show()` does nothing because shells start with FLAG_SHOWN).
FOX still routes events to `mouseGrabWindow`, and `ungrab()` releases it.
Verified in Xvfb with fluxbox: drag, switch desktop, drop into FeatherPad
(file opened, log "accepted"); drag, minimize Swordfish, grab kept, released
on button up.

Pre-build checks: Escape cancels a drag and releases the grab; internal drops
still show Copy/Move/Link; two back-to-back cross-desktop drags both reach
FeatherPad. The Places sidebar has never supported dragging. Tree-tab drags still
failed: hovering over a folder during the drag expands it, and
`DirList::onExpanded` called `ungrab()`, which also released the helper's grab.
It now skips the ungrab while `isDragging()`. Verified: tree folder dragged
to a copy-only target on desktop 2 arrived (`file:///go`). The
folder-expansion regression passes; its script needs `-lXrandr` added to the
link line (missing before this work).

Baseline test 3 (9 November, end of day): built with
`./portable/build-portable.sh build/Swordfish-0.0.3-beta1-baseline-test3`
(Debian 10, glibc 2.28) and served from
`build/baseline-test3-download-20261109/` on port 8766 (test 2 remains on
8765). SHA-256 of the portable tarball:
`722b906645878ff68042c3e9e2b8177471a233eea2f316712d9d5d4b51f497a8`. Smoke
test of the extracted package: started normally, and a file dragged to a
program on another desktop arrived ("accepted" in `ui.log`). It contains the
trash bar and sidebar Empty Trash, clearer history arrows, address-mode radio
buttons, the launcher/toolbar-size fixes, and all drag fixes above.

Plan: the user tests test 3 the next day (cross-desktop and panel drags to
Chrome/FeatherPad, Tree-tab drags, plain clicks, internal Copy/Move/Link menu,
toolbar size, launchers after restart, trash buttons, address bar). If all
pass, commit and push as the stable baseline, then branch for
modularization. Nothing is committed yet. Disk clean-up removed superseded
test packages (icon-startup, beta1-updates 1 and 2, baseline test 1) and
temporary test folders; kept are `release-beta1`, baseline tests 2 and 3, and
the user's notes and screenshots in `build/`.

### 3.51. Combined Home and mounted-drive Trash (10 November 2026)

The user tested baseline test 3 from a fresh extraction. Places -> Trash opened
the correct host Home Trash. A disposable file trashed directly from Home in
Caja appeared in both programs, but items from a mounted Projects drive were
missing in Swordfish. The previous host-path fix was necessary but incomplete:
Caja combines Home Trash and per-volume Trash; Swordfish listed Home only.

The user approved a combined view, Restore to each item's original location,
and Empty Trash applying to Home plus mounted drives after confirmation.
Older private portable Trash must not be migrated or deleted automatically.

Implementation:

- Shared helpers discover the user's `.Trash-<uid>` and sticky `.Trash/<uid>`
  layouts from mounted filesystems, reject symlink/foreign-owned roots and
  deduplicate directories. They do not create per-volume Trash folders.
- The Home Trash listing includes mounted-drive items, keeping each row's real
  backing path and independent identity even when filenames match. Hidden
  trashed items remain visible. Periodic refresh detects other-drive changes
  and unmounts. The address bar retains the Home Trash navigation anchor.
- Metadata parsing accepts either field order, decodes percent escapes once,
  resolves volume-relative paths against their mount point and rejects
  relative paths escaping that mount. Properties and both restore routes use
  the same reader. New metadata is percent-encoded, including literal percent
  signs; creating missing Trash directories no longer skips writing metadata.
- Restore, clipboard, file operations, Properties, archive inputs and selected
  size commands use physical item paths rather than joining a displayed name
  onto Home Trash. Metadata cleanup requires the source to have actually
  disappeared; successful File operations cover mounted-drive paths too.
- Empty Trash confirms its combined scope, checks each folder, stops on
  failure/cancellation, preserves metadata for remaining items, and recreates
  removed files/info directories privately. Trash Size uses the same discovered
  locations. Unmounted drives and legacy private portable Trash are untouched.
- Ordinary folder listings and file operations avoid mounted-Trash discovery
  unless their paths could be inside Trash. Read/discovery errors are logged.

Validation uses disposable Home/volume fixtures and the existing mount-table
interposition, never real desktop Trash. Added checks cover both volume
layouts, duplicate names and selection after refresh, hidden folders,
percent-encoded and unordered metadata, descendant paths, invalid escape/NUL
paths, first-use metadata creation, symlink decoys, mount disappearance,
actual clipboard/Properties, actual Restore, Empty confirmation cancellation,
and a simulated deletion error preserving the remaining restore metadata.

Clean native rebuild and the full folder/device regression passed, as did
the Custom Actions model and window-manager GUI regressions. One initial
folder/device run hit its existing 150ms UI timing assertion later in unrelated
share-reconnection checks; subsequent complete runs passed without relaxing
the threshold. The folder runner now links Xrandr only when the configured
build uses it, matching the GUI runner's convention.

Independent re-verification (clean `make -C src` rebuild, real tmpfs drive
mounted at `/media/sftestvol`, disposable Home Trash, app driven on Xvfb):
both Home and drive items listed with correct original locations (including a
`%20`-encoded name); Restore moved the drive item back to
`/media/sftestvol/projects/sfm/drive note.txt` and removed its record; Empty
Trash counted both locations, Cancel kept everything, OK emptied both; with the
drive remounted read-only, Home was emptied, the drive item and its record were
kept, errors were shown and the app stayed running; unmounting the drive left
a working Home-only Trash view. Custom Actions model and GUI regressions
passed. Known cosmetic point: the read-only case shows two error boxes (file,
then folder).

Baseline test 4 (10 November, morning): built with
`JOBS=2 ./portable/build-portable.sh build/Swordfish-0.0.3-beta1-baseline-test4`
(Debian 10, glibc 2.28, about 6 minutes). It adds the multi-volume Trash fix to
test 3. Downloads, source (working-tree, non-ignored files) and `SHA256SUMS`
are in `build/baseline-test4-download-20261110/`, served on port 8767.
Portable SHA-256:
`ba579821736adc9b841f2aa27c667c89cf4af27e617049c84ee9f8793b4b3e3d`.
Smoke test: the extracted package started normally and showed the welcome
window.

Updated the changelog, portable notes and manual. No commit, push or branch
was made; baseline test 3 downloads remain unchanged.

User test (10 November, 11:10 UTC): the user downloaded and tested test 4 on
their machine and reported "trash fix passed".

### 3.52. MX Blues built-in theme (10 November 2026)

The user supplied `build/MX Blue.swordfish-theme` (named "MX Blues" inside)
and asked for it as a built-in theme called MX Blues, right after Current
Theme (index 1). Added to `PreferencesBox` before Swordfish with the file's
exact colors; `NUM_DEFAULT_THEMES` raised from 14 to 15 (custom themes are
stored separately as `THEMES/themeN`, so they are unaffected). Verified on
Xvfb after a clean rebuild: the list shows Current Theme, MX Blues, Swordfish,
...; selecting MX Blues and Accept saved basecolor `#29353b`, backcolor
`#1e242d`, listbackcolor `#0f3540`, highlightcolor `#113d48`. Not in test 4.

### 3.53. FTP unmount URI lookup (10 November 2026)

A tester reported that mounting an FTP server from the address bar worked,
but Unmount showed "Can't determine the network mount URI". The uploaded
screenshot and `build/logs.tar.gz` confirm an FTP GVFS mount was opened and
browsed. `xf_gvfsmounturi()` handled SMB, SFTP and MTP but omitted FTP.

Added FTP to the existing host/user/port URI conversion, preserving encoded
usernames and targeting the mount root even from a nested directory. This
shared helper also supplies sidebar network-mount recognition. No mount,
authentication or unmount-command behavior was otherwise changed.

Incremental native build and the complete folder/network regression passed.
New assertions cover plain FTP, encoded login/custom port, nested paths and
missing host; real `File::mount()` calls with the fixture host `gio` verify
the two expected FTP unmount commands are executed. This is local fixture
verification, not a connection to the tester's server; real-server retesting
is pending. Not included in test 4; no new package, commit or push.

### 3.54. Network devices and read-only DLNA browsing (10 November 2026)

The user asked to extend discovery to FTP, SSH, media servers, printers and
network infrastructure, all before the baseline. Agreed scope: media is
read-only browsing (folders and server categories in the file pane, files
streamed to a player or the browser, Download saves copies); infrastructure
is advertised devices only, no port scanning; no "Send to TV".

Helper (`src/NetworkDiscovery.cpp`): SSDP M-SEARCH, mDNS (legacy unicast)
and WS-Discovery printers emit `SERVICE<TAB>kind<TAB>key<TAB>address<TAB>name
<TAB>url<TAB>sources` (kinds ftp, sftp, media, printer, router); local and
non-IPv4 addresses are skipped. Web pages are only accepted on the same host
(`sameHostPage`). New `--media LOCATION dlna://host:port/path` (SOAP Browse
with paging, read-only GVariant rows whose link is the stream URL) and
`--download URL FILE` (writes `FILE.part` with O_EXCL, links into place, never
overwrites, prints DONE).

GUI: `RemoteDirectory` treats `dlna://` as a server path (all share read-only
guards apply), keeps a media-server registry (persisted in `NETWORK MEDIA`),
and runs a sequential download queue with one "Download failed" summary.
`DirPanel` lists services as `SidebarItem::SERVICE` rows ("Name (kind)") and
opens them (media: pane; ftp/sftp: `resolveNetworkPath`; others: host
xdg-open). FilePanel streams/downloads; tabs are labelled with the server
name. Streaming via xdg-open disables startup notification, which otherwise
left a busy cursor (caught by the GUI regression).

Verified: clean native build without warnings; network-discovery, full
folder-expansion GUI (new device, media, stream and download checks),
custom-actions and custom-actions UI regressions. A real minidlna in the
codespace was browsed, downloaded from (byte-identical, no overwrite) and
discovered over SSDP from a Docker container; an Xvfb smoke of the patched
test-4 bundle opened its folders from the address bar. Not tested on a real
LAN, FTP/SFTP servers, printers or routers. No commit or push.

Baseline test 5 (10 November, 12:28 UTC): built with
`JOBS=2 ./portable/build-portable.sh build/Swordfish-0.0.3-beta1-baseline-test5`
(Debian 10, glibc 2.28). Adds network devices/DLNA browsing (this section),
MX Blues (3.52) and the FTP unmount fix (3.53) to test 4. Downloads, source and
`SHA256SUMS` are in `build/baseline-test5-download-20261110/`, served on port
8767. Portable SHA-256:
`033ffc65a44124c1160de3431b541ce80639bf8e13ce3ed147d02f28ba999b05`.
Smoke test: the extracted package started and exited cleanly; its
`swordfish-network` includes the `--media`/`--download` modes.

### 3.55. Media play choices and download progress (10 November 2026)

The user tested test 5 with Kodi and Jellyfin: double-click always went to
the browser (only Swordfish's own player setting was consulted, and the stream
is an http address), and downloads gave no feedback.

- Double-click order is now: the Swordfish audio/video player, then the
  desktop's default application for the file's MIME type (guessed from the
  name with GIO, looked up with `xf_defaultApplications`), then the browser
  through host `xdg-open`. Desktop applications run through `xf_hostcommand`
  with the stream URL appended.
- The media file context menu (one file selected) has **Play**, **Play With**
  (installed applications whose `MimeType` includes the type, the default
  first, plus **Web Browser**), **Copy Stream Address** (text clipboard) and
  **Download...**. Folders keep **Open / Download... / Refresh**.
- The helper's `--download` prints `PROGRESS <bytes> <total|-1>` lines
  (throttled to 250 ms) before `DONE`, and removes its `.part` file and exits
  130 on SIGTERM/SIGINT.
- `RemoteDirectory::download` now takes an owner window. The queue parses the
  progress lines and shows a non-modal **Media Download** window after 0.5 s
  (so quick downloads don't flash it): "Downloading N of M: name", a bar
  (bytes when the size is known, else files) and the byte count. **Hide**
  keeps it hidden until the batch ends; **Cancel Downloads**
  (`RemoteDirectory::cancelDownloads`) terminates the helper, removes the
  `.part` file, drops the queue and reports no error.

Verified: clean native build without warnings; network-discovery (new
progress, sized and SIGTERM cleanup checks), full folder-expansion GUI (menu
contents, clipboard, default application, Play With, browser, progress
window, Hide, Cancel), custom-actions and custom-actions UI regressions. The
GUI test now isolates system application folders. Not yet tested with real
Kodi/Jellyfin or a real player. No commit or push.

Baseline test 6 (10 November, 13:16 UTC): built with
`JOBS=2 ./portable/build-portable.sh build/Swordfish-0.0.3-beta1-baseline-test6`
(Debian 10, glibc 2.28). Test 5 plus this section. Downloads, source and
`SHA256SUMS` are in `build/baseline-test6-download-20261110/`, served on port
8767 (test 5 server stopped). Portable SHA-256:
`0c9dedcf720431204215f3f6cd390fae29eada3fab6c663202f8e456b11890b0`.
Smoke test: the extracted package started under Xvfb and kept running; its
binaries contain the new menu items and the helper's PROGRESS output.

Feedback on test 6: **Play With** listed applications that cannot play the
file (it took every application whose `MimeType` includes the type, such as
editors), and offered no way to reach the regular Open With dialog. Changes:

- `DesktopApplication` gained `acceptsUrls` (Exec has `%u`/`%U`). Besides the
  desktop default, Play With lists only applications that accept URLs and
  have a `Player`, `TV` or `Viewer` category, excluding `WebBrowser` ones
  (covered by **Web Browser**).
- **Other Application...** opens the regular Open With dialog.
  `HistInputDialog::setOpenWithContext` takes an optional known MIME type,
  because `file` cannot inspect a stream. "Remember" saves a Swordfish
  association for the extension.
- Double-click now also honours a Swordfish association that is a real
  command; `<placeholder>` associations still only map to the audio/video
  player settings. Commands outside the bundle (`swordfish-open`, `swf*`,
  `xfw`) run through `xf_hostcommand`, like the regular Open With.

Verified: clean rebuild without warnings; all four suites pass. The GUI test
adds an editor and a `%F`-only player that must not appear, plus Other
Application with and without Remember. No commit or push.

Baseline test 7 (10 November, 13:40 UTC): built with
`JOBS=2 ./portable/build-portable.sh build/Swordfish-0.0.3-beta1-baseline-test7`
(Debian 10, glibc 2.28). Test 6 plus the Play With filtering and Other
Application. Downloads, source and `SHA256SUMS` are in
`build/baseline-test7-download-20261110/`, served on port 8767 (test 6 server
stopped). Portable SHA-256:
`9185071f2574da1d19cb9a4374f10d9fdb452307c0cc053f430cd978359bc20d`.
Smoke test: the extracted package started under Xvfb and kept running.

### 3.56. Swordfish 0.0.3 beta 2 released (10 November 2026)

The user tested baseline test 7 ("all done") and asked to commit, push and
publish beta 2. `configure.ac` and `configure` now say `0.0.3-beta2`; README,
portable/README, the manual, RELEASE-NOTES and the CHANGELOG heading were
updated. Release tag: `swordfish-0.0.3-beta2`; title **Swordfish 0.0.3 beta
2**. As with beta 1 (3.39), it carries GitHub's Latest badge and is not
flagged as a pre-release; 0.0.2 stays the stable fallback, unchanged.

Files: portable ZIP and tar.gz and the source archive from the tag workflow,
plus `Swordfish-User-Manual-0.0.3-beta2.zip` added by hand, with
`Swordfish-0.0.3-beta2-SHA256SUMS` covering all four. The commit also carries
regenerated autotools/gettext files (`aclocal.m4`, `Makefile.in`, `po/*.po`);
the release container regenerates them with `autoreconf -fi` regardless.

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

- **Superseded by sections 3.40-3.42:** 0.0.3 beta 1 is published; its
  startup fix is committed locally as `a62bc2e`, tested by the user and
  unpushed. Subsequent dialog and device-label changes are uncommitted and
  await real-machine testing. The items below are kept as history.
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

0.0.2 is the current frozen stable release; 0.0.3 beta 1 is published.
Sections 3.40-3.42 record the latest state: the user-tested startup fix is
local and unpushed; the subsequent dialog and device-label changes are
uncommitted and await real-machine testing. Start there, not from an old
experimental ZIP, an obsolete roadmap or an incomplete chat summary. Do not
publish local work without the user's permission.
