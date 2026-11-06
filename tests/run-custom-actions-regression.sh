#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
run_dir="$root/tests/.custom-actions-run-$$"
trap 'rm -rf "$run_dir"' EXIT HUP INT TERM
mkdir -p "$run_dir"
export TMPDIR="$run_dir"
${CXX:-g++} ${CXXFLAGS:-} -std=c++11 -Wall -Wextra -I"$root/src" $(fox-config --cflags) \
    "$root/tests/custom-actions-regression.cpp" "$root/src/CustomActions.cpp" \
    $(fox-config --libs) -o "$run_dir/regression"
"$run_dir/regression" "$run_dir/actions.rc"
