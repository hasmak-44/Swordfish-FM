#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
run_dir="$root/tests/.system integration-%-run-$$"
trap 'rm -rf "$run_dir"' EXIT HUP INT TERM
mkdir -p "$run_dir/home"
mkdir -p "$run_dir/usr/bin"

${CXX:-g++} ${CXXFLAGS:-} -std=c++11 -Wall -Wextra -Werror -fsyntax-only \
    "$root/src/swordfish-command-link.cpp"

${CXX:-g++} ${CXXFLAGS:-} -std=c++11 -Wall -Wextra \
    '-DSWORDFISH_COMMAND_LINK_HELPER="/nonexistent/swordfish-command-link"' \
    "$root/tests/system-integration-regression.cpp" "$root/src/SystemIntegration.cpp" \
    -o "$run_dir/usr/bin/swordfish"

HOME="$run_dir/home" \
XDG_CONFIG_HOME="$run_dir/portable/config" \
XDG_DATA_HOME="$run_dir/portable/data" \
SWORDFISH_HOST_XDG_CONFIG_HOME="$run_dir/home/.config" \
SWORDFISH_HOST_XDG_DATA_HOME="$run_dir/home/.local/share" \
SWORDFISH_HOST_PATH="$run_dir/home/.local/bin" \
PATH="/usr/bin:/bin" \
"$run_dir/usr/bin/swordfish"
