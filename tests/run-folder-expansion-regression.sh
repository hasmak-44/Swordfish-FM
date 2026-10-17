#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
icons=${1:-/usr/share/swordfish/icons/default-theme}
if [[ ! -d "$icons" ]]; then
    echo "Pass an installed or portable default-theme icon directory as the first argument." >&2
    exit 1
fi
prefix=$(cd -- "$icons/../../../.." && pwd)
scratch=$(mktemp -d /tmp/swordfish-expansion-test.XXXXXX)
trap 'rm -f "$scratch/main.o" "$scratch/test" "$scratch/swordfish-network"; rm -rf "$scratch/fixture" "$scratch/home"; rmdir "$scratch"' EXIT
cp "$root/tests/fake-remote-directory.py" "$scratch/swordfish-network"
chmod +x "$scratch/swordfish-network"
mkdir -p "$scratch/fixture/folder" "$scratch/fixture/empty" "$scratch/home"
touch "$scratch/fixture/folder/nested.txt"
ln -s folder "$scratch/fixture/link-folder"
ln -s missing "$scratch/fixture/broken-link"
mkdir "$scratch/fixture/large"
for ((i=0; i<793; ++i)); do
    mkdir -p "$scratch/fixture/large/folder-$i/subfolder/subsubfolder"
done
objcopy --redefine-sym main=swordfish_main "$root/src/main.o" "$scratch/main.o"
objects=()
for object in "$root"/src/*.o; do
    case "$(basename "$object")" in
        main.o|NetworkDiscovery.o|swordfish_preview-PreviewWorker.o|WriteWindow.o|XFileArchive.o|XFileImage.o|XFilePackage.o|XFileWrite.o) ;;
        *) objects+=("$object");;
    esac
done
g++ -std=gnu++11 -Wall -I"$root/src" -I"$root" $(fox-config --cflags) \
    "$root/tests/folder-expansion-regression.cpp" "$scratch/main.o" "${objects[@]}" \
    "$root/st/x.o" "$root/st/st.o" "$root"/libsn/*.o \
    -lutil -lfontconfig -lpng -lFOX-1.6 -lX11 -lfreetype -lXft -lxcb -lxcb-util \
    -lX11-xcb -ldl $(pkg-config --libs gio-2.0) -o "$scratch/test"
runner=()
if [[ ${SWORDFISH_TEST_DEBUG:-0} == 1 ]]; then
    runner=(gdb --batch -ex run -ex bt --args)
fi
HOME="$scratch/home" timeout 50s xvfb-run -a "${runner[@]}" "$scratch/test" "$scratch/fixture" "$prefix/bin/swordfish" "$icons"
