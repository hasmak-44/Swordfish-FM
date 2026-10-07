<p align="center">
  <img src="logo.jpg" width="250" alt="Swordfish-FM Logo" />
</p>

![Swordfish FM Banner](banner.jpg)

# Swordfish FM

> **⚠️ Work in Progress - Not ready for release yet**
> 
> About the name **SwordFish**, The original XFE file manager is known for its speed and versatility, ad I am a life long avid angler, I wanted to choose a name that reflects both the ethos of XFE, and combine with my hobby, Swordfish is one of the fastest and most agile top ocean predators, hence the name ;}
> 
> I hope **SwordFish-FM** will live up to expectations and satisfies your needs.

## 🙏 Credits & Acknowledgments

Swordfish FM is based on **Xfe (X File Explorer)** — a lightweight file manager for X11.

- **Original Author & Maintainer of Xfe:** [Roland Baudin (roland65)](https://github.com/roland65/xfe)
- **Based on:** X Win Commander by Maxim Baranov
- **Homepage:** http://roland65.free.fr/xfe/

All credit for the original Xfe codebase goes to Roland. Swordfish FM is a modern continuation / fork inspired by his work.

## 📜 License

This project is based on Xfe, which is licensed under the **GNU General Public License v2.0 (GPLv2)**.

- Original Xfe License: GPLv2
- Swordfish FM will maintain GPLv2 compatibility.

See [COPYING](COPYING) for full details..
Copyright (C) Roland Baudin and contributors.

> If **XFE** is the root, **Swordfish FM** aims to be the evolution.

## Portable Linux testing build

A portable Linux build lets you run Swordfish without installing it into the
system. It currently supports 64-bit x86 Linux.

### Download and run Swordfish

You do not need to compile the program or install developer tools.

1. Open this repository's **Actions** tab on GitHub.
2. In the workflow list, choose **Portable build artifacts**.
3. Open a completed run for the portable version you want. A green check mark
   means the build succeeded.
4. Near the bottom of the run page, find **Artifacts** and click the
   `Swordfish-Portable-...` download.
5. Extract the downloaded ZIP file. Open the extracted `Swordfish-Portable`
   folder.
6. Double-click `AppRun` to start Swordfish. If Linux asks whether to run the
   file, choose **Run**. You can also start it from a terminal by opening that
   folder and running `./AppRun`.

Keep the extracted folder together: `AppRun` needs the files beside it. Your
portable settings and data are saved in that folder, so you can move or back
up the whole folder. This is a testing build, not a system-wide installation.

The ZIP and `.tar.gz` downloads are attached to the workflow run, not to the
tag page. GitHub's **Source code** downloads on a tag page are the program's
source files, not the ready-to-run portable build. Workflow downloads are
available for 90 days.

For technical details or to build it yourself, see
[portable/README.md](portable/README.md).
