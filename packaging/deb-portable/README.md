# Swordfish FM portable .deb (concept)

This is a concept for MX Linux reviewers. The `swordfish-portable` package
installs the official, already-tested Swordfish FM portable build as a
self-contained folder, instead of spreading files across `/usr`.

## What happens

1. **Install.** `apt install ./swordfish-portable_0.0.3-1_amd64.deb`, MX
   Package Installer or Synaptic. The package itself only places a staging
   archive in `/usr/lib/swordfish-portable/`, a menu entry and menu icons.
2. **Choose a location.** The installer finds the user who started the
   installation (sudo, pkexec or the active desktop session) and opens a
   folder chooser on that user's desktop (yad, otherwise zenity). The default
   is `~/Applications`. Without a desktop, it asks a debconf question instead.
   Unattended installs (`DEBIAN_FRONTEND=noninteractive`) use the default.
3. **Copy.** A `Swordfish-Portable` folder is created inside the chosen
   folder, owned by that user. Settings stay in
   `Swordfish-Portable/portable-data`, so the folder can still be copied to a
   USB stick or another computer. Each install uses a new staging folder;
   existing staging folders are left untouched.
4. **Menu and command.** "Swordfish FM (Portable)" appears under System /
   File Manager, and `/usr/local/bin/swordfish-portable` links to the copy's
   `AppRun`, so `swordfish-portable` works in a terminal.
5. **Upgrade.** Installing a newer package replaces the program files in the
   same location and keeps `portable-data`.
6. **Move.** `sudo dpkg-reconfigure swordfish-portable` asks again and moves
   the copy, with its settings, to the new location.
7. **Remove.** Removing the package deletes the copied folder, including its
   settings, the command link and the menu entry.

The installer refuses system folders (`/usr`, `/etc`, `/var`, ...), folders
the user can't write to (except `/opt`, which it creates and hands to the
user) and existing `Swordfish-Portable` folders it didn't create. Removal only
deletes a folder that carries the package's marker file.

## Building

```sh
gh release download swordfish-0.0.3 -p 'Swordfish-Portable-0.0.3.tar.gz'
packaging/deb-portable/build-deb.sh -o /tmp/deb Swordfish-Portable-0.0.3.tar.gz
```

The archive can be for amd64, i386, armhf or arm64. The architecture is
taken from `-a ARCH`, otherwise from a `-ARCH` suffix in the archive name
(`Swordfish-Portable-<version>-arm64.tar.gz`), otherwise amd64. The script
refuses an archive whose programs are for a different architecture.
`-r` sets the Debian revision (default 1). Set `DEB_MAINTAINER` to change
the maintainer field.

For an isolated Debian install, reinstall and removal regression check:

```sh
sh tests/run-portable-deb-regression.sh /tmp/deb/swordfish-portable_0.0.3-1_amd64.deb
```

The check uses Docker and may download the `debian:bookworm-slim` image. It
runs the image for the package's architecture, so testing a package for
another architecture needs QEMU emulation registered as described in
`portable/README.md`.

## Questions for reviewers

These are deliberate departures from Debian policy, to discuss:

- **Files outside dpkg's control.** The copy in the user's folder isn't
  listed by `dpkg -L`; maintainer scripts create and delete it.
- **`/usr/local/bin`.** Policy 9.1.2 says packages shouldn't create files
  there. `/usr/bin/swordfish-portable` would be the policy-friendly choice.
- **Interactive maintainer script.** A graphical dialog from `postinst` is
  unusual; debconf is used whenever no desktop is available.
- **One user.** The copy belongs to the installing user, but the menu entry
  is system-wide. Other users would see an entry that can't open that user's
  private copy.
- **Bundled libraries.** The portable build carries its own libraries (built
  on Debian 10 / glibc 2.28), so they don't receive Debian security updates.
- **Removal deletes settings.** Even a plain `remove` deletes
  `portable-data`, not only `purge`.

A conventional package built from the source release
(`Swordfish-FM-0.0.3-source.tar.gz`) is the alternative if the repository
prefers system-installed files.
