#!/bin/bash
echo "Installing icon links..."
set -e
SRC="$1"
DST="$2"

mkdir -p "$DST/icons/gnome-theme" "$DST/icons/xfce-theme" "$DST/icons/kde-theme" "$DST/icons/default-theme"

# Link default-theme icons into other themes if missing
shopt -s nullglob
for f in "$SRC"/icons/default-theme/*.png "$SRC"/icons/apps/48x48/apps/*.png; do
  [ -f "$f" ] || continue
  bn=$(basename "$f")
  for th in gnome-theme xfce-theme kde-theme; do
    if [ ! -f "$SRC/icons/$th/$bn" ] && [ ! -f "$DST/icons/$th/$bn" ]; then
      ln -sf "../default-theme/$bn" "$DST/icons/$th/$bn" 2>/dev/null || true
    fi
  done
done

for f in "$SRC"/icons/default-theme/*.png; do
  bn=$(basename "$f")
  for th in gnome-theme xfce-theme kde-theme; do
    if [ ! -f "$SRC/icons/$th/$bn" ] && [ ! -f "$DST/icons/$th/$bn" ]; then
      ln -sf "../default-theme/$bn" "$DST/icons/$th/$bn" 2>/dev/null || true
    fi
  done
done
echo "Icon links done."
