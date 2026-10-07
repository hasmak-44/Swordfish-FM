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
system-wide. These instructions are for MX Linux 23 and other Debian-based
systems. The installer needs Python 3, `curl`, GitHub CLI (`gh`), and Python's
Tk folder picker.

### One-time setup on MX Linux 23 or Debian

Open a terminal and run these commands to add GitHub's official package
repository and install the required tools:

```sh
sudo apt update
sudo apt install curl ca-certificates
sudo mkdir -p -m 755 /etc/apt/keyrings
curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg | sudo tee /etc/apt/keyrings/githubcli-archive-keyring.gpg > /dev/null
sudo chmod go+r /etc/apt/keyrings/githubcli-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" | sudo tee /etc/apt/sources.list.d/github-cli.list > /dev/null
sudo apt update
sudo apt install gh python3 python3-tk
```

You only need to do this setup once.

### Other Linux distributions

Install `gh` (GitHub CLI), Python 3, and `curl` using your distribution's
package manager. Common GitHub CLI package commands are:

| Linux distribution | Install GitHub CLI |
| --- | --- |
| Fedora | `sudo dnf install gh` |
| Arch Linux or Manjaro | `sudo pacman -S github-cli` |
| openSUSE | `sudo zypper install gh` |
| Alpine | `sudo apk add github-cli` |
| Void Linux | `sudo xbps-install github-cli` |

The installer also needs a folder picker. It uses an installed `zenity`,
KDE's `kdialog`, or Python Tkinter. If none is available, install the
folder-picker package provided by your distribution (often named `zenity`,
`kdialog`, or `python3-tkinter`) and run the installer again. The exact package
name can vary. If your distribution is not listed above, use the
[official GitHub CLI Linux installation guide](https://github.com/cli/cli/blob/trunk/docs/install_linux.md)
to install `gh`.

### Download and start Swordfish

Open a terminal and copy/paste this command. It downloads and starts the installer:

```sh
curl -fL https://raw.githubusercontent.com/hasmak-44/Swordfish-FM/portable-layout/portable/install-swordfish.py -o /tmp/install-swordfish.py && python3 /tmp/install-swordfish.py
```

The first time, GitHub CLI will guide you through signing in using your
browser. This is a one-time step. The installer then finds and downloads the
latest successful portable build, opens a folder picker so you can choose
where to put it, and starts Swordfish. Your installed files and portable
settings stay together in a version-named folder under the location you
choose. It will not overwrite an existing installation. Leave the terminal
open while Swordfish is running; if anything goes wrong, the installer prints
an error there.

Build downloads are kept for 90 days. If the installer says no build is
available, a new portable build needs to be created. GitHub's **Source code**
downloads on a tag page are not the ready-to-run app.

For technical details or to build it yourself, see
[portable/README.md](portable/README.md).
