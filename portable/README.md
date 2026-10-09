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

The portable launcher leaves `HOME` unchanged, so file navigation still starts
from the user's real home directory. Portable mode reads settings from the
sidecar, uses resources from its own `usr/share/swordfish`, and does not load
installed XFE or Swordfish registry defaults. Icon themes can be selected from
the bundled resources or from folders under the sidecar `themes` and `icons`.
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
