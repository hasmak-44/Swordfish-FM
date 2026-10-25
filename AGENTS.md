# AGENTS.md

## Project overview

Swordfish FM is a fork of the XFE/X11 file manager, with the main application code in `src/` and packaging/resources in the repo root, `icons/`, and `portable/`. Most of the project is built with autotools and a FOX/X11 toolchain.

- Main project docs: [README.md](README.md)
- Portable bundle notes: [portable/README.md](portable/README.md)
- Build/configuration logic: [configure.ac](configure.ac)
- Packaging metadata: [Makefile.am](Makefile.am)

## Repository conventions

- Keep changes small and consistent with the existing XFE/Swordfish C++ style in `src/`.
- Prefer surgical edits over introducing new dependencies or framework layers.
- Respect the project’s GPLv2 heritage and X11 desktop assumptions.
- Do not hand-edit generated autotools artifacts unless the task explicitly requires it. This includes files such as `configure`, `config.h`, `Makefile.in`, `aclocal.m4`, and generated desktop/translation artifacts.
- When changing resources, icon themes, or install metadata, verify the related `Makefile.am` and desktop/package files before assuming a build is unaffected.

## Build and validation

Use the repo root for normal local builds:

```sh
./autogen.sh
./configure
make
```

Useful variants:

```sh
./configure --prefix=/usr/local
make
make install
```

For the portable Linux bundle:

```sh
./portable/build-portable.sh
```

This repo does not appear to include a dedicated automated test harness; for validation, prefer a clean build and a focused smoke check around the change.

## Code layout

- `src/`: core application code and XFE-style widgets
- `icons/`: bundled icon/theme assets
- `po/`: translations and gettext templates
- `automount/`: desktop automount support
- `portable/`: relocatable build packaging and launcher assets

## Good default behavior for agents

- Start by checking the existing code patterns in the relevant file before changing APIs or class names.
- Keep GUI behavior, startup logic, and resource paths consistent with the rest of the application.
- For packaging or install-file changes, verify both the source file and the generated build flow.
- Favor precise, repo-native fixes over broad refactors.
