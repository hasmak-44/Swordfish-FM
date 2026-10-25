# Copilot instructions for Swordfish FM

## Project overview

Swordfish FM is a fork of the XFE/X11 file manager, built as a native C++ app using the FOX toolkit and X11 desktop conventions. The main implementation lives under `src/`; packaging, icons, translations, and portable bundle logic live in the repo root, `icons/`, `po/`, `automount/`, and `portable/`.

Important references:
- `README.md` and `README` for project background and build/install notes
- `AGENTS.md` for repo-specific conventions
- `configure.ac` and `Makefile.am` for the autotools build
- `portable/README.md` for the portable Linux bundle

## Build, validation, and smoke checks

This repo is primarily built with autotools; there is no dedicated unit-test or lint harness in the checked-in sources.

Use the project root for normal local builds:

```sh
./autogen.sh
./configure
make
```

Common variants:

```sh
./configure --prefix=/usr/local
make
make install
```

Portable Linux bundle:

```sh
./portable/build-portable.sh
```

Validation guidance:
- There is no repo-defined `test` or `lint` target to run. Treat a successful `make` as the baseline validation step.
- For a focused smoke check after a change, rebuild the app and launch the binary from `src/` (for example `./src/swordfish`) in an X11 session, or use the portable bundle if the change is packaging-related.
- If you change build metadata, install metadata, resource links, or icon/theme assets, verify the related `Makefile.am`, generated build files, and any affected desktop/package integration files before concluding the change is safe.

## High-level architecture

The codebase is organized around an XFE-style single-application GUI rather than a modern multi-package service layout:

- `src/main.cpp` boots the app and initializes the FOX/X11 environment.
- `src/XFileExplorer.{h,cpp}` is the main window and top-level controller for menus, toolbars, tabs, window layout, bookmarks, and panel state.
- `src/FilePanel.{h,cpp}`, `src/DirPanel.{h,cpp}`, and `src/FileList.{h,cpp}` implement the file browsing panes and their behavior.
- `src/SearchWindow.*`, `src/TextWindow.*`, `src/CommandWindow.*`, and the dialog helpers under `src/` handle search results, text viewing/editing, command execution, and other modal workflows.
- `icons/` and the top-level resource files provide default theme/icon assets and desktop metadata; `portable/` contains the relocatable Linux bundle and launcher logic.
- `automount/` and integration files around the repo handle desktop mount support and platform-specific behavior.

The important architectural pattern is that most behavior is implemented as a set of FOX widget classes that share app state rather than as separate services or frameworks. In practice, understanding the app usually requires reading the main window and the panel/dialog classes together.

## Key conventions

- Keep changes small and consistent with the existing XFE/Swordfish C++ style in `src/`.
- Prefer surgical edits over introducing new dependencies or large abstractions.
- Respect the project’s GPLv2 heritage and X11 desktop assumptions; do not rewrite the app around a non-X11 or non-FOX model unless the task explicitly requires it.
- Before changing APIs or class names, inspect the neighboring widget classes and existing event/message patterns in the same subsystem.
- Do not hand-edit generated autotools output such as `configure`, `config.h`, `Makefile.in`, `aclocal.m4`, and generated desktop/translation artifacts unless the task explicitly requires it; regenerate via the normal autotools flow instead.
- When changing resources, icons, package metadata, or install metadata, verify the related source and build logic together (`Makefile.am`, desktop files, `icons/`, `portable/`, and the generated output paths).
- Preserve existing behavior around window layout, file-panel synchronization, bookmarks, and root-mode integration; the project is highly GUI and workflow sensitive.

## Repo-specific guidance from existing docs

The repo already documents the same broad conventions in `AGENTS.md` and the project README:
- Use the repo root for normal builds.
- Prefer targeted, repo-native edits over broad refactors.
- Treat the application as an X11/FOX file manager rather than a web or cross-platform toolkit app.
- Validate packaging and desktop integration changes with the build flow, not just the source tree.
