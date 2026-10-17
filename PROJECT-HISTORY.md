# Swordfish FM: project history and recovery guide

Last updated: 17 October 2026, after publication of 0.0.1 RC4.

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
- Places/Bookmarks/Devices-only sidebar visibility and hidden-root toggle safety.
- Legacy tree deep-path synchronization.
- Real archive-style command followed by real signal-driven remote completion.
- Preview codecs, cache/invalidation, failed decoding, deadlines and child cleanup.
- Mocked discovery-root recovery behavior, not real LAN discovery success.

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

### Discovery and network management

Discovery is **not fixed** and is hidden in RC4. Desired future coverage includes
SMB, Bonjour/mDNS, FTP/SFTP/WebDAV, NFS, optional AFP and UPnP/DLNA.
The desired flow is background/progressive discovery, stop/refresh/disable,
grouping services by server and showing capabilities, with no surprise mounts
or credential collection. The latest direction for media services was external
browser/player opening rather than an internal DLNA catalogue/player.

No real LAN discovery test succeeded in the build environment; mocked helper
tests do not establish actual SMB discovery. Keep this separate from the
successfully tested mounted-GVFS native loader.

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
