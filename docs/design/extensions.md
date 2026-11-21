# Swordfish FM extensions: discussion and design direction

Recorded from the 21 November 2026 discussion.

**Status: planning only.** This document records the user's decisions and the
implementation ideas discussed around them. Swordfish does not yet implement
this extension API, Cloud tab, account wizard, casting extension or email
extension. No manifest format or message schema has been finalized.

## Agreed principles

- Extensions are separate modules that extend or add Swordfish functions.
  They are downloaded separately and communicate with Swordfish through an
  API or similar interface, rather than being built into the file manager.
- Extensions may be developed in any programming environment.
- **Every extension is one self-contained, portable folder.** Its programs,
  dependencies, assets and user data all nest inside that folder. This rule
  applies to every extension, not just cloud integration.
- Extensions bundle their own binaries, libraries or runtimes. In particular,
  **rclone belongs to the Cloud extension download, not to Swordfish**.
- Installation should be as simple as dropping the extension folder into
  Swordfish's extensions directory.
- Copying an extension folder should carry its settings and data with it.
  In a portable installation, extensions should travel with `portable-data`.
- Cloud integration is the first priority, as an rclone front end supporting
  multiple accounts. It should have a separate tab in the left pane so a large
  account list does not crowd Places.
- Full synchronization is deferred. The first cloud work should concentrate
  on account configuration, mounting and browsing.

## Proposed host architecture and API

The proposed model is an out-of-process extension program. Swordfish starts
it and exchanges versioned JSON messages over stdin/stdout; JSON-RPC was
suggested as a possible protocol. This keeps the interface language-neutral
and avoids loading extension code as a shared library inside the FOX process.
Failure and timeout handling would still need designing; process separation
alone does not provide a permission sandbox.

Possible API contributions discussed:

| Contribution | Examples |
| --- | --- |
| File and background context-menu actions, filtered for the selection | Cast selected files; cloud actions |
| Sidebar tabs and places with changing status | Cloud accounts; pinned mounted accounts |
| Toolbar buttons and dropdowns | Account selection |
| Status, notifications and progress | Mounting, uploads, casting |
| Settings pages | Initially a simple manifest-driven form |
| Send-to destinations | Email selected files |
| Calls into Swordfish | Read the selection; open a path or tab; refresh a folder |

Swordfish should draw contributed sidebar rows and forms using its own theme
rather than embedding an unrelated interface. A manifest would describe the
extension's identity, version, supported platforms, API compatibility,
launcher, contributions, declared permissions and system prerequisites.
`extension.ini` was an example name, not an agreed file format.

The host would discover folders, check compatibility, ask before enabling a
new extension, start/stop its process and handle messages. Permission
declarations must not be presented as enforced restrictions unless an actual
enforcement mechanism exists. An initial fake extension could exercise a
left-pane tab, changing status and opening a folder before real cloud work.
Custom Actions might later become a simple extension type; no migration or
replacement of the existing feature was agreed.

## Folder layout, portability and updates

An illustrative layout:

```text
extensions/
  cloud/
    extension.ini
    cloud-extension
    bin/
      rclone
    providers/
      providers.ini
      icons/
    data/
      rclone.conf
      cache/
      logs/
    licenses/
```

The proposed portable location is `portable-data/extensions/cloud/`. The
installed-build search location still needs defining.

An earlier suggestion put accounts/cache/logs in a separate
`extension-data/cloud/` directory. **The final single-folder decision
supersedes that suggestion.** All extension-owned persistent data must remain
under the extension's parent folder. Internal saved paths should be relative
and resolved against its current location.

Proposed lifecycle safeguards:

- Updates replace program files while preserving nested user data. A common
  `data/` directory was suggested, but its API contract is not finalized.
  Blindly replacing the whole folder would lose accounts and is not an
  acceptable update procedure.
- Removing the folder also removes its nested data. Uninstall guidance must
  explain this and offer a backup; deleting a running extension or a cloud
  mount with pending writes is not a safe lifecycle operation.
- The host's enable/disable choice and permission approvals could remain in
  Swordfish settings. This was a host-policy proposal, not permission to
  scatter an extension's own data elsewhere. A copied extension may require
  consent again on its new host.
- Include dependency versions and license notices in the extension.
- State supported architectures and reject incompatible downloads with a
  clear message. Separate downloads per architecture were preferred for size;
  a folder bundling multiple architectures was also discussed.
- Self-contained packaging does not replace operating-system facilities.
  Cloud mounting still needs suitable kernel/FUSE support and a usable mount
  helper. Detect missing prerequisites and explain them rather than silently
  failing.

The future Swordfish build targets are amd64, i386, armhf and arm64; see the
[portable build documentation](../../portable/README.md). Adding those targets
to scripts is not proof that every extension or architecture has been tested.

## Cloud extension: first scope

The extension should use its **own bundled rclone and its own nested config**,
not silently adopt a system rclone or modify the user's unrelated rclone
configuration. Importing existing remotes could be an explicit optional step.

The suggested backend is one rclone remote-control process (`rclone rcd`)
instead of starting a new command for every action. Listing remotes,
mount/unmount actions, supported quota information and transfer statistics
would flow through it. Its local endpoint, authentication and lifecycle need
specifying before implementation.

Mounted accounts appear as local folders, allowing existing file panels,
tabs, bookmarks and transfers to work without a new virtual-filesystem layer.
A full remote filesystem API would require a larger refactor and was deferred.

### Cloud tab

The proposed separate left-pane tab would offer:

- Accounts grouped by provider, a search filter and an Add account button.
- States such as not mounted, mounting, mounted and error.
- Used/free space where the backend supports it; unavailable quota must not
  be shown as zero or treated as a failed connection.
- Mount-and-open in the current tab or a new tab.
- Context actions for unmount, refresh, open in a new tab, pin to Places and
  edit/reconnect an account.
- Visibility of pending uploads and account failures.

### Account setup and provider presets

The user requested a familiar, preconfigured list of popular providers because
login methods, required credentials and two-factor authentication differ.
The list should supply names, icons, backend identifiers, preset options,
friendly labels, help links and sensible mount defaults. It must not duplicate
or hard-code rclone's changing authentication state machine.

Login categories discussed, with illustrative provider examples:

| Category | Proposed user flow |
| --- | --- |
| Browser/OAuth, such as Drive, OneDrive or Dropbox | Open the provider's sign-in page; the provider handles password and 2FA |
| Interactive password/code flows | Prompt through the backend's actual setup steps, including 2FA when required |
| App passwords, such as some WebDAV services | Explain where to create the provider's app password |
| Access keys, such as S3-compatible services or B2 | Ask for the backend's required keys, region and endpoint |
| Server login, such as SFTP | Ask for host and the supported password/key authentication |

These are planning categories, not verified promises of backend support or
login behavior. Verify them against the rclone version bundled in the extension.
The suggested starting list was Google Drive, OneDrive, Dropbox, Box, pCloud,
MEGA, Proton Drive, Nextcloud/WebDAV, Backblaze B2, Amazon S3, Wasabi and SFTP.
Other examples discussed included iCloud Drive, ownCloud, Koofr, Cloudflare R2,
IDrive e2, Yandex, Zoho, SMB and FTP.

An **Other provider** option would expose additional backends with less
curated guidance. rclone's provider metadata and non-interactive configuration
continuation were suggested for generating questions, filling preset answers,
hiding optional advanced fields and handling provider-specific steps. Exact
commands, flags and supported flows must be checked before coding.

Proposed add-account sequence:

1. Choose a provider from searchable icons.
2. Give the account a recognizable display name and generate a safe internal
   remote identifier.
3. Complete the provider/backend login flow.
4. Test root listing and, where supported, show quota.
5. Choose read-only/read-write, optional startup mounting and preview settings.
6. Add the account to the Cloud tab.

Reconnect should reuse the account rather than delete it when authorization
expires. Optional custom OAuth client IDs were discussed as an advanced
setting, not a first-version requirement.

### Credentials, cache and background work

Credential storage is **unresolved**. A system keyring was initially suggested,
but it does not travel with a copied extension. The later discussion included
an encrypted rclone config with a session unlock and a portable master-password
prompt; any unencrypted-storage choice would require explicit explanation and
consent. rclone password obscuring must not be described as encryption.
No passwords or tokens should be logged or stored in Swordfish's general
settings file. Secret storage and migration need a reviewed design.

Full sync remains out of scope, but normal writable mounts can still queue
uploads. A file-manager copy finishing is not proof that the remote upload has
finished. Show pending uploads and warn before unmount/exit when they remain.

Network-expensive thumbnails, previews and recursive folder sizing were
suggested as off by default on cloud mounts, with per-account opt-in.
Cache size limits and an explicit Clear cache action were also discussed.
Never clear or omit pending-write data as if it were disposable cache.
Automatic cache omission while copying a folder was an idea, not an approved
exception to the requirement that copying an extension carries its data.
Detect stale mounts after crashes; recovery must not discard queued uploads.

## Casting and email

The original candidate list was multi-account cloud integration, a
multi-account email client and casting to a TV.

Casting was initially suggested as the easiest proof of concept. The later
discussion prioritized cloud and suggested a small fake cloud-facing test
extension instead: casting alone would not test cloud tabs, mounts or account
status. Casting remains a possible second extension, not a committed schedule.

Casting ideas included DLNA, Chromecast and AirPlay targets, a Cast-to submenu,
a temporary local media HTTP server and a separate play/pause/seek/stop/queue
window. Existing UPnP discovery could inform the implementation, but there is
no implemented extension discovery API and no verified compatibility promise
for those protocols.

For email, a full multi-account client was recognized as a much larger separate
application. A narrower Send to email/account action for selected attachments,
an optional recent-attachments place and an extension-owned compose window
were proposed instead. The user has not finalized that scope.

## Next design decisions

Before implementation, review the extension API and manifest, installed and
portable discovery paths, process lifecycle, Cloud tab contributions,
credential protection, update/data preservation, mountpoint placement and
copy/export behavior for mounted remotes, and mount/upload shutdown behavior.
Confirm provider coverage against a pinned rclone version.
The discussion authorized recording and refining the design, not implementing
a broad plugin framework or shipping the proposed extensions.
