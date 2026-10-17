#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
scratch=$(mktemp -d /tmp/swordfish-network-test.XXXXXX)
trap 'rm -f "$scratch/network-test" "$scratch/servers/NAS"; rmdir "$scratch/servers" "$scratch"' EXIT
mkdir "$scratch/servers"
touch "$scratch/servers/NAS"
g++ -std=gnu++11 -Wall $(pkg-config --cflags gio-2.0) \
    "$root/tests/network-discovery-regression.cpp" $(pkg-config --libs gio-2.0) \
    -o "$scratch/network-test"
timeout 10s "$scratch/network-test" "$scratch/servers"
