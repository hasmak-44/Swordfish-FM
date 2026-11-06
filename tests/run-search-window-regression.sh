#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
scratch=$(mktemp -d '/tmp/swordfish search-%-test.XXXXXX')
trap 'rm -rf "$scratch"' EXIT
mkdir -p "$scratch/usr/bin" "$scratch/usr/share/swordfish/icons" \
    "$scratch/fixture/poison" "$scratch/fixture/data" "$scratch/home"
ln -s "$root/icons/default-theme" "$scratch/usr/share/swordfish/icons/default-theme"
ln -s "$root/docs/manual" "$scratch/usr/share/swordfish/manual"
printf 'needle\n' > "$scratch/fixture/data/match.txt"
printf 'other\n' > "$scratch/fixture/data/other.txt"
# An unversioned SELinux fixture reproduces the reported loader warning.
cc -shared -fPIC -x c -o "$scratch/fixture/poison/libselinux.so.1" - <<'C'
#include <stdlib.h>
int is_selinux_enabled(void) { return 0; }
void freecon(char* context) { free(context); }
int getfilecon(const char* path, char** context) { return -1; }
int lgetfilecon(const char* path, char** context) { return -1; }
C
LD_LIBRARY_PATH="$scratch/fixture/poison" find "$scratch/fixture/data" -type f \
    > "$scratch/baseline.out" 2> "$scratch/baseline.err"
grep -q 'no version information available' "$scratch/baseline.err"
printf 'PASS: reproduced the reported libselinux version warning with an unversioned fixture\n'
objcopy --redefine-sym main=swordfish_main "$root/src/main.o" "$scratch/main.o"
objects=()
for object in "$root"/src/*.o; do
    case "$(basename "$object")" in
        main.o|swordfish-command-link.o|NetworkDiscovery.o|swordfish_preview-PreviewWorker.o|WriteWindow.o|XFileArchive.o|XFileImage.o|XFilePackage.o|XFileWrite.o) ;;
        *) objects+=("$object");;
    esac
done
read -r -a configured_flags <<< "$(sed -n 's/^CXXFLAGS = //p' "$root/src/Makefile")"
g++ -std=gnu++11 "${configured_flags[@]}" -I"$root/src" -I"$root" $(fox-config --cflags) \
    "$root/tests/search-window-regression.cpp" "$scratch/main.o" "${objects[@]}" \
    "$root/st/x.o" "$root/st/st.o" "$root"/libsn/*.o \
    -lutil -lfontconfig -lpng -lFOX-1.6 -lX11 -lfreetype -lXft -lxcb -lxcb-util \
    -lX11-xcb -ldl $(pkg-config --libs gio-2.0) -o "$scratch/usr/bin/swordfish"
runner=()
if [[ ${SWORDFISH_TEST_DEBUG:-0} == 1 ]]; then
    runner=(gdb --batch -ex run -ex bt --args)
fi
HOME="$scratch/home" timeout 60s xvfb-run -a "${runner[@]}" "$scratch/usr/bin/swordfish" "$scratch/usr" "$scratch/fixture"
