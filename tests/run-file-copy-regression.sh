#!/bin/bash
set -euo pipefail
ulimit -c 0
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
native=${1:-"$root"}
native=$(cd -- "$native" && pwd)
run_dir=$(mktemp -d /tmp/swordfish-file-copy-regression-XXXXXXXX)
trap 'rm -rf "$run_dir"' EXIT

objcopy --redefine-sym main=swordfish_main "$native/src/main.o" "$run_dir/main.o"
objects=()
for object in "$native"/src/*.o; do
    case "$(basename "$object")" in
        main.o|swordfish-command-link.o|NetworkDiscovery.o|swordfish_preview-PreviewWorker.o|WriteWindow.o|XFileArchive.o|XFileImage.o|XFilePackage.o|XFileWrite.o) ;;
        *) objects+=("$object");;
    esac
done
read -r -a configured_flags <<< "$(sed -n 's/^CXXFLAGS = //p' "$native/src/Makefile")"
randr_libs=()
if [[ " ${configured_flags[*]} " == *" -DHAVE_XRANDR_H=1 "* ]]; then
    randr_libs=(-lXrandr)
fi
${CXX:-g++} -std=gnu++11 -Wall "${configured_flags[@]}" -I"$native" -I"$root/src" \
    "$root/tests/file-copy-regression.cpp" "$run_dir/main.o" "${objects[@]}" \
    "$native/st/x.o" "$native/st/st.o" "$native"/libsn/*.o \
    -lutil -lfontconfig -lpng -lFOX-1.6 -lX11 -lfreetype -lXft -lxcb -lxcb-util \
    -lX11-xcb -ldl "${randr_libs[@]}" $(pkg-config --libs gio-2.0) -o "$run_dir/copy-regression"
xvfb-run -a -s "-screen 0 1280x900x24" "$run_dir/copy-regression"
