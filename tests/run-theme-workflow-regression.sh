#!/bin/bash
set -euo pipefail
ulimit -c 0
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
scratch=$(mktemp -d /tmp/swordfish-theme-test.XXXXXX)
trap 'rm -rf "$scratch"' EXIT
mkdir -p "$scratch/usr/bin" "$scratch/usr/share/swordfish/icons" \
    "$scratch/fixture/folder" "$scratch/home"
ln -s "$root/icons/default-theme" "$scratch/usr/share/swordfish/icons/default-theme"
ln -s "$root/icons/preferences" "$scratch/usr/share/swordfish/icons/preferences"
ln -s "$root/textures" "$scratch/usr/share/swordfish/textures"
ln -s "$root/themes" "$scratch/usr/share/swordfish/themes"
printf 'preview fixture\n' > "$scratch/fixture/real-preview-file.txt"
objcopy --redefine-sym main=swordfish_main "$root/src/main.o" "$scratch/main.o"
objects=()
for object in "$root"/src/*.o; do
    case "$(basename "$object")" in
        main.o|swordfish-command-link.o|NetworkDiscovery.o|swordfish_preview-PreviewWorker.o|WriteWindow.o|XFileArchive.o|XFileImage.o|XFilePackage.o|XFileWrite.o) ;;
        *) objects+=("$object");;
    esac
done
read -r -a configured_flags <<< "$(sed -n 's/^CXXFLAGS = //p' "$root/src/Makefile")"
randr_libs=()
if [[ " ${configured_flags[*]} " == *" -DHAVE_XRANDR_H=1 "* ]]; then
    randr_libs=(-lXrandr)
fi
g++ -std=gnu++11 "${configured_flags[@]}" -I"$root/src" -I"$root" $(fox-config --cflags) \
    "$root/tests/theme-workflow-regression.cpp" "$scratch/main.o" "${objects[@]}" \
    "$root/st/x.o" "$root/st/st.o" "$root"/libsn/*.o \
    -lutil -lfontconfig -lpng -lFOX-1.6 -lX11 -lfreetype -lXft -lxcb -lxcb-util \
    -lX11-xcb "${randr_libs[@]}" $(pkg-config --libs gio-2.0) -o "$scratch/usr/bin/swordfish"
runner=()
if [[ ${SWORDFISH_TEST_DEBUG:-0} == 1 ]]; then
    runner=(gdb --batch -ex run -ex bt --args)
fi
HOME="$scratch/home" XDG_CONFIG_HOME="$scratch/home/.config" timeout 90s xvfb-run -a -s '-screen 0 1600x1100x24' \
    "${runner[@]}" "$scratch/usr/bin/swordfish" "$scratch/usr" "$scratch/fixture"
