#!/bin/bash
set -euo pipefail

source_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_root="$source_root/build"
output_dir=${1:-"$build_root/Swordfish-Portable"}
output_dir=$(realpath -m -- "$output_dir")
docker_command=${DOCKER:-docker}
image=swordfish-portable-builder:debian12

if ! command -v "$docker_command" >/dev/null 2>&1; then
    printf 'Docker is required to build against the Debian 12 glibc baseline.\n' >&2
    exit 1
fi

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

jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')}
if ! [[ "$jobs" =~ ^[1-9][0-9]*$ ]]; then
        printf 'JOBS must be a positive integer, got: %s\n' "$jobs" >&2
        exit 1
fi

relative_output=${output_dir#"$build_root"/}
if [ "$relative_output" = "$output_dir" ] || [ "$relative_output" = "$build_root" ]; then
        printf 'Cannot map output path into build directory: %s\n' "$output_dir" >&2
        exit 1
fi

# Create the bind-mount source as the invoking user; otherwise Docker may
# create it as root and the unprivileged container user cannot write to it.
mkdir -p "$build_root"

"$docker_command" build \
        --tag "$image" \
        --file "$source_root/portable/Dockerfile" \
        "$source_root/portable"

"$docker_command" run --rm \
        --user "$(id -u):$(id -g)" \
        --env HOME=/tmp/swordfish-portable-home \
        --env JOBS="$jobs" \
        --volume "$source_root:/source:ro" \
        --volume "$build_root:/output" \
        "$image" \
        /usr/local/bin/build-portable-in-container "$relative_output"

archive="${output_dir}.tar.gz"
tar -C "$(dirname -- "$output_dir")" -czf "$archive" "$(basename -- "$output_dir")"
printf 'Compressed portable bundle created at %s\n' "$archive"
