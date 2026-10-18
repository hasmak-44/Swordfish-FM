#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
scratch=$(mktemp -d /tmp/swordfish-network-test.XXXXXX)
trap 'rm -f "$scratch/network-test"; rmdir "$scratch"' EXIT
g++ -std=gnu++11 -Wall $(pkg-config --cflags gio-2.0) \
    "$root/tests/network-discovery-regression.cpp" $(pkg-config --libs gio-2.0) \
    -o "$scratch/network-test"
timeout 20s "$scratch/network-test"
