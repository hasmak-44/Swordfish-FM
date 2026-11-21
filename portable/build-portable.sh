#!/bin/bash
set -euo pipefail

source_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_root="$source_root/build"
output_dir=${1:-"$build_root/Swordfish-Portable"}
output_dir=$(realpath -m -- "$output_dir")
docker_command=${DOCKER:-docker}
base_image=${BASE_IMAGE:-debian:buster-slim}

if ! command -v "$docker_command" >/dev/null 2>&1; then
    printf 'Docker is required to build against an old glibc baseline.\n' >&2
    exit 1
fi

# ARCH uses Debian names. Other architectures than the host's run under
# QEMU user emulation, which must be registered with binfmt_misc first.
case "$(uname -m)" in
    x86_64) host_arch=amd64 ;;
    i?86) host_arch=i386 ;;
    aarch64|arm64) host_arch=arm64 ;;
    armv7*|armv8l) host_arch=armhf ;;
    *) host_arch=$(uname -m) ;;
esac
arch=${ARCH:-$host_arch}
case "$arch" in
    amd64) platform=linux/amd64 ;;
    i386) platform=linux/386 ;;
    armhf) platform=linux/arm/v7 ;;
    arm64) platform=linux/arm64 ;;
    *)
        printf 'Unsupported architecture %s; use amd64, i386, armhf or arm64.\n' "$arch" >&2
        exit 1
        ;;
esac
image=swordfish-portable-builder:${base_image//[:\/]/-}-$arch

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
        --platform "$platform" \
        --tag "$image" \
        --build-arg BASE_IMAGE="$base_image" \
        --file "$source_root/portable/Dockerfile" \
        "$source_root/portable"

"$docker_command" run --rm \
        --platform "$platform" \
        --env SWORDFISH_ARCH="$arch" \
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
