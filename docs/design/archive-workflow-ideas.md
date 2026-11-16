# Archive folders: ideas, workarounds and possible solutions

Status: **design notes only, not implemented** (12 November 2026).

This document records a long discussion about slow and unsafe archive folders.
The only change shipped from it is that ZIP archives open read-only (see
"Current behaviour"). Everything else here is a proposal for later. Build and
benchmark history is in [PROJECT-HISTORY.md](../../PROJECT-HISTORY.md)
sections 3.6, 3.62 and 3.63.

Guiding principle agreed with the user: **data integrity matters more than
speed.** A faster backend that can damage an archive is not acceptable.

## 1. Current behaviour

"Open as Folder" mounts an archive with archivemount (FUSE) under a folder
next to it. "Close Archive Folder" unmounts it; for writable mounts,
archivemount saves the changes when it unmounts.

| Format | Mode | Why |
|---|---|---|
| tar, tar.gz, tgz, tar.bz2, tar.xz, tar.zst... | read/write | archivemount saves these correctly |
| ZIP (by extension or by file signature) | **read-only** | protects user data, see below |
| 7z, RAR, ISO, CAB, others | read-only | archivemount can't write them |
| any archive without write permission | read-only | |

Code: `archiveWritable`, `archiveSignature` and `onCmdArchiveOpen` in
`src/FilePanel.cpp`. A read-only ZIP shows a "ZIP archive opened read-only"
notice saying it is read-only to protect the user's data, and that files can
still be browsed and dragged or copied out.

### Why ZIP is read-only

- archivemount 0.8.7 (the Debian/Ubuntu version) **silently rewrites a
  modified ZIP as a tar archive** while keeping the `.zip` name. Other
  programs then can't open it.
- Both 0.8.7 and archivemount-ng store newly added entries with a leading `/`;
  `unzip` warns and exits with status 1 when extracting them.
- archivemount keeps a `.orig` backup, but users don't expect it and it
  doubles disk use.

### What Swordfish checks after saving a tar archive

`archiveVerifySave` in `src/FilePanel.cpp` runs after a writable archive
folder is closed.

It catches:
- a save in the wrong format (signature changed)
- a missing or empty archive after saving
- a truncated or unreadable archive (`tar -tf` fails)

It does not catch:
- wrong contents inside a valid archive, or missing entries
- odd entry names (such as a leading `/`)
- a missing `tar`, so the check can't run
- archives unmounted outside Swordfish

**Possible improvement:** record the name and size of every file in the
mounted folder before closing, then compare them with `tar -tvf` of the saved
archive. Warn, and keep the `.orig` backup, if anything differs.

## 2. Speed: archivemount and alternatives

Copying out of an archive folder is slow with archivemount 0.8.7, especially
for large files in compressed archives. archivemount-ng 1c was 10 to 25 times
faster for large files and about the same for many small files (PROJECT-HISTORY
§3.62).

Not adopted because:
- the speed gain doesn't outweigh the write-safety problems above
- it isn't packaged by most distributions, so it would have to be bundled
- it hasn't had format, write and Swordfish integration testing

Other FUSE options worth testing later, read-only: `fuse-archive` (libarchive,
fast, read-only by design), `ratarmount` (tar and others, indexes large
archives, Python), `fuse-zip` (ZIP read/write, native ZIP writer), `squashfuse`
and kernel `mount -o loop` for ISO (needs root or udisks).

## 3. Workaround: bypass archivemount by extracting to a folder

Idea: extract the whole archive with 7-Zip (or another tool) into a
temporary folder and show that folder; on close, recompress it.

| For | Against |
|---|---|
| no FUSE, works in containers and portable mode | extracts everything before anything is visible: slow for a 10 GB ISO |
| any format the tool can read | needs free space for the whole unpacked content |
| editing is ordinary file editing | recompressing writes a whole new archive (another full copy) |

The archiver doesn't need to be 7-Zip. Choose the tool per format:

| Format | Read | Write |
|---|---|---|
| tar family | `tar`, `bsdtar` | `tar` |
| ZIP | `unzip`, `7z`, `bsdtar` | `zip`, `7z` |
| 7z | `7z`/`7zz`/`7za` | `7z` |
| RAR | `unrar`, `7z` (Linux 7-Zip includes RAR reading) | only proprietary `rar` |
| ISO | `7z`, `bsdtar`, `xorriso` | `xorriso`, `bsdtar` |
| CAB, CPIO, LZH, ARJ | `7z`, `bsdtar`, native tools | native tools only |

libarchive (`bsdtar`, or linking the library) reads nearly everything and
writes tar, ZIP, 7z, ISO and CPIO. It could replace most separate tools.

## 4. Proposed solution: archive-manager-style virtual folder

Behave like an archive manager (Ark, File Roller, 7-Zip on Windows), but
inside the normal file list:

1. **Open** lists contents without extracting anything (`7z l -slt`,
   `bsdtar -tvf` or libarchive) and shows the tree as a virtual folder.
2. **Drag out, copy or open** extracts only the selected entries, into the
   destination or a temporary folder.
3. **Drag in, rename, delete or edit** are staged as pending changes, not
   written straight into the archive.
4. **Save** writes a **new** archive in the same folder (`name.zip.partial`),
   tests it (`7z t`, `unzip -t`, `tar -tf` plus a name and size comparison),
   and only then replaces the original with an atomic `rename()`. If any step
   fails, the original is untouched and the partial file is removed.
5. Opening is reasonably fast, there is little overhead for read-only use, and
   the original archive is never modified in place.

Disk space: saving needs room for the new archive beside the original. A
10 GB ISO needs another 10 GB. Check this when opening and **before** the user
starts editing: if there isn't room, open read-only and say why.

## 5. Disk-space checking

- `statvfs()` free space (`f_frsize * f_bavail`) is the basic check.
- Free space can be wrong: quotas, btrfs/ZFS estimates, and network shares.
  A better test is to **ask the file system to allocate** the space:
  create a hidden temporary file, `fallocate()` it to the needed size, then
  delete it. ENOSPC, EDQUOT or EFBIG means it won't fit. This is instant on
  ext4, XFS and btrfs. If `fallocate` isn't supported, fall back to `statvfs`.
- Unpacked size, by format:
  - plain tar: the archive size (exact)
  - compressed tar or single compressed file: archive size is only a lower bound
  - ZIP: `unzip -Zt` ("N bytes uncompressed")
  - others: `7z l` summary line, or the sum of sizes from `bsdtar -tvf`
  - run every listing command with `timeout` so a damaged archive can't hang the UI
- Limit: the space can't stay reserved while tar, unzip or 7z write the files
  themselves. Another program can still fill the disk during extraction.
  Ways to reduce the risk:
  - extract into a hidden `.name.partial` folder and rename it only when
    extraction succeeds, so a failed extraction never looks finished
  - for single compressed files (`.gz`, `.xz`...), preallocate the output file
    and decompress into it, so the space stays reserved for the whole write
- Message when there isn't enough room: title "Not Enough Disk Space", naming
  the archive, the space needed ("at least" when only a lower bound is known),
  the free space in the destination, and "Nothing was extracted."

## 6. Drop menu: Extract Here and Compress Here

When archives are dragged to another folder or partition, the drag-and-drop
menu (Copy Here, Move Here, Link Here) could also offer:

- **Extract Here**, only when every dropped item is an archive:
  - check write permission and disk space first
  - extract each archive into a **new folder** named after it ("Photos",
    then "Photos (2)" if that exists), so nothing spills out or is overwritten
  - decompress a single compressed file (`notes.txt.gz`) to a new unique name
  - if no tool is installed, say to install 7-Zip or bsdtar
- **Compress Here...**, for any local items:
  - open the existing archive-name dialog (ArchInputDialog) with the drop
    folder filled in
  - items must come from one folder; otherwise explain and stop
  - single-file formats (`.gz`, `.bz2`, `.xz`, `.zst`, `.Z`) only for exactly
    one regular file, written with `gzip -c FILE > ARCHIVE`, **never in place**
- Show both entries on any drop, not only across partitions. A plain drop
  never extracts or compresses by default.

The existing per-format commands are a poor fit here: `gunzip`, `bunzip2` and
`unxz` extract next to the source rather than the drop target, and
`gzip -v FILE` replaces the source.

### Implementation patch

A working draft of sections 5 and 6 was written and then removed until the
user decides to implement it:
[archive-extract-compress-here.patch](archive-extract-compress-here.patch).
It passed `g++ -fsyntax-only` on every changed file but was **never built or
run**. It applies with `git apply docs/design/archive-extract-compress-here.patch`
to the tree at the time it was written. It contains:

- `src/xfeutils.{h,cpp}`: `xf_isarchive`, `xf_archivebasename`,
  `xf_extractcommand`, `xf_archiveunpackedsize`, `xf_reservespace`,
  `xf_checkextractspace`
- `src/File.{h,cpp}`: `File::dropPaths`, `File::allArchives`,
  `File::extractInto`, `File::compressInto`
- `src/FileList.{h,cpp}`: `ID_DRAG_EXTRACT`, `ID_DRAG_COMPRESS` and the menu
  entries (the folder tree's DirList menu isn't covered yet)
- `src/FilePanel.cpp`: space checks in Extract, Extract to Folder and Extract
  Here, and opening a writable archive read-only when there's no room to save it
- `tests/search-window-regression.cpp`: base-name and space-reservation checks

It changes headers, so run `make -C src clean` before building it.

## 7. Related issues found along the way

- `FilePanel::onCmdExtractHere` computes `ext1` with
  `name.section('\t', 0).rafter('.', 1); lower();`, so the extension is never
  lowercased: an upper-case name like `ARCHIVE.ZIP` falls through to the
  default `tar` command. Not fixed.
- The portable build scripts don't bundle archive tools (`7z`, `unrar`,
  `xorriso`, `bsdtar`); Swordfish uses whatever the host has installed.

## 8. Open questions

- Use libarchive directly, or keep calling command-line tools?
- Is the virtual folder worth it, or is the drop menu (section 6) enough?
- Should ZIP become writable again once there is a safe save path (section 4)?
- Which FUSE backend, if any, to test next for faster read-only browsing?
