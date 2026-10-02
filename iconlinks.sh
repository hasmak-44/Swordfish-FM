#!/bin/sh
echo Installing icon links...
mkdir -p "$2/icons/gnome-theme" "$2/icons/xfce-theme" "$2/icons/kde-theme"
for f in $1/icons/default-theme/*.png $1/icons/apps/*.png 2>/dev/null; do
 [ -f "$f" ] || continue
 bn=$(basename "$f")
 for th in gnome-theme xfce-theme kde-theme; do
  [ -f $1/icons/$th/$bn ] || ln -sf ../default-theme/$bn $2/icons/$th/$bn 2>/dev/null; ln -sf ../apps/$bn $2/icons/$th/$bn 2>/dev/null; true
 done
done
for f in $1/icons/default-theme/*.png; do
 bn=$(basename "$f"); for th in gnome-theme xfce-theme kde-theme; do [ -f $1/icons/$th/$bn ] || ln -sf ../default-theme/$bn $2/icons/$th/$bn; done
done
