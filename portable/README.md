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
The bundle also provides the original Xfe executable names (`xfa`, `xfi`,
`xfp`, and `xfw`) as compatibility aliases for saved settings and file
associations.

Preferences under Programs offers three application modes: use the host
desktop's default applications for every file, use Swordfish's built-in
applications where available (and the desktop defaults for other files), or
customize each program and file association individually. The default is to
use desktop defaults. Host XDG application associations are used without
moving Swordfish's own configuration out of the portable sidecar.

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
- `logs` contains persistent, categorized application logs:
  - `application.log` records startup and shutdown.
  - `ui.log` records layout, panel, tab, hidden-file, and thumbnail changes.
  - `file-operations.log` records file opens, navigation, and requested
    copy/move/rename/link/trash/delete operations.
  - `settings.log` records saved program and panel preferences.
  - `errors.log` records selected operational failures, such as unavailable
    associated programs or denied directory access.

Log entries include timestamps and process IDs. Logging is enabled
automatically; it records semantic actions and relevant paths, not raw
keystrokes or mouse movement. Coverage will expand as more actions are
instrumented.

The portable launcher leaves `HOME` unchanged, so file navigation still starts
from the user's real home directory. Portable mode reads settings from the
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

## Sharing color themes

In Settings → Appearance, custom color themes can be exported to a
`*.swordfish-theme` text file (name, tip and 12 `#RRGGBB` colors) and imported
on another machine with the Import/Export buttons or the theme context menu.
