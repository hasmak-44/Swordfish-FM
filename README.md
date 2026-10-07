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
system-wide and does not require installing GitHub CLI, Python, or other
installer tools.

### Download and start Swordfish

1. Open the [Portable build artifacts workflow](https://github.com/hasmak-44/Swordfish-FM/actions/workflows/portable-release.yml)
   in your web browser.
2. Select the newest run with a green check mark.
3. Scroll down to **Artifacts** and click the
   `Swordfish-Portable-...` download. Your browser downloads a ZIP file.
4. Open your **Downloads** folder and extract the downloaded ZIP using your
   file manager's archive option (often **Extract Here** or **Extract To**).
   This first ZIP contains the portable app ZIP.
5. Extract the `Swordfish-Portable-...zip` inside it to the folder where you
   want to keep Swordfish.
6. Open the extracted `Swordfish-Portable` folder and double-click `AppRun`.
   If Linux asks, choose **Run**.

Keep the extracted `Swordfish-Portable` folder together. Swordfish saves its
settings and data inside that folder, so you can move or back it up as one
unit. This is a testing build, not a system-wide installation.

The artifact download is available for 90 days. GitHub's **Source code**
downloads on a tag page are not the ready-to-run app.

For technical details or to build it yourself, see
[portable/README.md](portable/README.md).
