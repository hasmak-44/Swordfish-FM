#!/bin/bash
set -euo pipefail

source_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_root="$source_root/build"
output_dir=${1:-"$build_root/Swordfish-Portable"}
output_dir=$(realpath -m -- "$output_dir")

if [ "$(uname -m)" != x86_64 ]; then
    printf 'This portable build currently targets Linux x86_64, not %s.\n' "$(uname -m)" >&2
    exit 1
fi

case "$output_dir" in
    "$build_root"/*) ;;
    *)
        printf 'Refusing to build outside %s: %s\n' "$build_root" "$output_dir" >&2
        exit 1
        ;;
esac

linuxdeploy=${LINUXDEPLOY:-linuxdeploy}
if ! command -v "$linuxdeploy" >/dev/null 2>&1; then
    printf 'linuxdeploy is required to bundle shared libraries. Install it or set LINUXDEPLOY.\n' >&2
    exit 1
fi

jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')}
if ! [[ "$jobs" =~ ^[1-9][0-9]*$ ]]; then
    printf 'JOBS must be a positive integer, got: %s\n' "$jobs" >&2
    exit 1
fi

cd "$source_root"
./configure --prefix=/usr
make -j"$jobs"

rm -rf -- "$output_dir"
mkdir -p "$output_dir"
DESTDIR="$output_dir" make install

deploy_args=(--appdir "$output_dir")
for program in swordfish swfa swfi swfp swfw; do
    executable="$output_dir/usr/bin/$program"
    if [ ! -x "$executable" ]; then
        printf 'Expected installed program is missing: %s\n' "$executable" >&2
        exit 1
    fi
    deploy_args+=(--executable "$executable")
done

desktop_file="$output_dir/usr/share/applications/swordfish.desktop"
if [ ! -f "$desktop_file" ]; then
    printf 'Expected desktop file is missing: %s\n' "$desktop_file" >&2
    exit 1
fi
deploy_args+=(--desktop-file "$desktop_file")
"$linuxdeploy" "${deploy_args[@]}"

install -m 755 portable/AppRun "$output_dir/AppRun"
install -m 644 portable/README.md "$output_dir/README.md"
mkdir -p "$output_dir/usr/share/doc/swordfish"
install -m 644 COPYING "$output_dir/usr/share/doc/swordfish/COPYING"
for program in swfa swfi swfp swfw; do
    ln -s AppRun "$output_dir/run-$program"
done

for directory in config data cache state scripts themes icons addons; do
    mkdir -p "$output_dir/portable-data/$directory"
done

printf 'Portable bundle created at %s\n' "$output_dir"
printf 'Launch it with: %s/AppRun\n' "$output_dir"
