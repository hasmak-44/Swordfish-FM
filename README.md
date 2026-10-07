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

This testing build runs on 64-bit x86 Linux. It does not install Swordfish
system-wide. The optional helper uses Python 3's standard library only; it
does not need GitHub CLI or extra Python packages. If Python 3 is not already
available, you can use the browser-only steps below instead.

### Download and start Swordfish

1. Download the [latest-build helper script](https://raw.githubusercontent.com/hasmak-44/Swordfish-FM/portable-layout/portable/download-latest-portable.py).
   You need Python 3 to run it; it does not need GitHub CLI or extra Python
   packages. If you prefer not to run the helper, open the
   [portable builds page](https://github.com/hasmak-44/Swordfish-FM/actions/workflows/portable-release.yml)
   and choose a successful run yourself.
2. Open a terminal in the folder where the script was downloaded and run
   `python3 download-latest-portable.py`. The helper finds the newest
   successful build and opens its page in your browser.
3. On that page, click the matching `Swordfish-Portable-...` name under
   **Artifacts**. Your browser downloads the ZIP wherever it normally saves
   downloads.
4. Find the downloaded ZIP and extract it to the place where you want to keep
   Swordfish. The extracted `Swordfish-Portable` folder contains the complete
   app. **Extract the whole folder; do not move files out of it. Keep the
   folder together.**
5. To update an existing copy, extract the new `Swordfish-Portable` folder
   over the old one in the same location. If asked, choose to replace the
   existing app files. Your settings and customizations in `portable-data`
   stay in place. To keep an older version too, extract the new folder
   somewhere different; both versions can be run side by side.
6. To start Swordfish, open the `Swordfish-Portable` folder and double-click
   `AppRun`. If Linux asks, choose **Run**. You can also open a terminal in
   that folder and run `./AppRun`, or create a desktop launcher whose command
   points to the `AppRun` file in your Swordfish folder.

This testing build runs on 64-bit x86 Linux and does not install Swordfish
system-wide. GitHub keeps each build download for 90 days. The helper opens
the correct build page, but GitHub requires the user to click the artifact
there; a browser does not allow scripts to start a file download silently.
The **Source code** downloads on a tag page are not the ready-to-run app.

For technical details or to build it yourself, see
[portable/README.md](portable/README.md).
