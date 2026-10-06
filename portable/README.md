# Portable Linux build

The portable build produces a relocatable AppDir-style directory for Linux
x86_64. It includes the Swordfish programs, installed resources, and shared
libraries collected by `linuxdeploy`. Run `AppRun` from the generated folder;
do not run `make install` as root.

The source must first be configured and compiled on a Linux system with the
Swordfish build dependencies installed. `linuxdeploy` is also required to
collect the shared libraries:

```sh
./portable/build-portable.sh
```

The output is `build/Swordfish-Portable`. It can be copied as a folder and
started with `build/Swordfish-Portable/AppRun`. The `run-swfa`, `run-swfi`,
`run-swfp`, and `run-swfw` launchers start the corresponding companion apps.

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
build therefore reduces conflicts with installed copies but cannot guarantee
compatibility with every Linux distribution. Test the folder on the target
systems before relying on it.

The first build is not yet an AppImage; it creates the relocatable directory
so its isolation can be tested before adding the AppImage packaging step.
