<p align="center">
  <img src="logo.jpg" width="250" alt="Swordfish-FM Logo" />
</p>

![Swordfish FM Banner](banner.jpg)

# Swordfish FM

For the development history, confirmed decisions, known limitations and recovery
instructions if the conversation is lost, read
[Project history and recovery guide](PROJECT-HISTORY.md).

> **Swordfish-Portable v0.0.1 — first stable release**
>
> 0.0.1 brings on-demand Network discovery of Windows and Linux file-sharing
> computers, browsing and mounting their shares, a Devices list with visible
> Eject/Unmount buttons, protected Root/Home, safe unmounting that closes every
> tab on the device first, inline rename, tab hover switching and the
> sidebar/view redesign. Earlier release candidates remain available.
> 
> About the name **SwordFish**, The original XFE file manager is known for its speed and versatility, ad I am a life long avid angler, I wanted to choose a name that reflects both the ethos of XFE, and combine with my hobby, Swordfish is one of the fastest and most agile top ocean predators, hence the name ;}
> 
> I hope **SwordFish-FM** will live up to expectations and satisfies your needs.

## 🙏 Credits & Acknowledgments

Swordfish FM development and modifications by **Hassanein Makhlouf (hasmak-44)**,
with AI-assisted development.

Swordfish FM is based on **Xfe (X File Explorer)** — a lightweight file manager for X11.

- **Original Author & Maintainer of Xfe:** [Roland Baudin (roland65)](https://github.com/roland65/xfe)
- **Based on:** X Win Commander by Maxim Baranov
- **Homepage:** http://roland65.free.fr/xfe/

All credit for the original Xfe codebase goes to Roland. Swordfish FM is a modern continuation / fork inspired by his work.

Existing copyright notices and GPL licence terms remain applicable.
For project communications and bug reports, please use
[GitHub Issues](https://github.com/hasmak-44/Swordfish-FM/issues).

## 📜 License

This project is based on Xfe, which is licensed under the **GNU General Public License v2.0 (GPLv2)**.

- Original Xfe License: GPLv2
- Swordfish FM will maintain GPLv2 compatibility.

See [COPYING](COPYING) for full details..
Copyright (C) Roland Baudin and contributors.

> If **XFE** is the root, **Swordfish FM** aims to be the evolution.

## Portable Linux release

A portable Linux build lets you run Swordfish without installing it into the
system. It currently supports 64-bit x86 Linux.

### Download and run Swordfish

The portable build runs on 64-bit x86 Linux. It does not install Swordfish
system-wide or require extra software to download it.

1. Open the [Swordfish 0.0.1 release](https://github.com/hasmak-44/Swordfish-FM/releases/tag/swordfish-0.0.1)
   and download **Swordfish-Portable-0.0.1.zip** under **Assets** (recommended).
   Your browser downloads the ZIP using its normal download settings.
2. Extract the ZIP to the place where you want to keep Swordfish. The ZIP
   contains the complete `Swordfish-Portable-0.0.1` folder. **Extract the whole
   folder; do not move files out of it. Keep the folder together.**
3. To update an existing copy, copy the contents of the extracted folder
   into your existing Swordfish folder. If asked, choose to replace the
   existing app files. Your settings and customizations in `portable-data`
   stay in place. To keep an older version too, extract the new folder
   somewhere different; both versions can be run side by side.
4. To start Swordfish, open the extracted Swordfish folder and double-click
   `AppRun`. If Linux asks, choose **Run**. You can also open a terminal in
   that folder and run `./AppRun`, or create a desktop launcher whose command
   points to the `AppRun` file in your Swordfish folder.

The portable build is not a system-wide installation.

#### Which download should I choose?

| Asset | Purpose |
| --- | --- |
| **Swordfish-Portable-0.0.1.zip** | **Recommended.** Release package built against Debian 10 / glibc 2.28 and launch-tested before publication. |
| Swordfish-Portable-0.0.1.tar.gz | The same release package in tar.gz format. |
| Swordfish-FM-0.0.1-source.tar.gz | Source code, not a ready-to-run application. |

Use `Swordfish-0.0.1-SHA256SUMS` to verify the downloads.
You need only one portable package.

For technical details or to build it yourself, see
[portable/README.md](portable/README.md).
