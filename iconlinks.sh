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
  if [ ! -f "$DST/icons/default-theme/$bn" ]; then
    cp "$f" "$DST/icons/default-theme/$bn"
  fi
  for th in gnome-theme xfce-theme kde-theme; do
    if [ ! -f "$SRC/icons/$th/$bn" ] && [ ! -f "$DST/icons/$th/$bn" ]; then
      ln -sf "../default-theme/$bn" "$DST/icons/$th/$bn"
    fi
  done
done

echo "Icon links done."
