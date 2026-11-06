#!/bin/bash
set -euo pipefail
ulimit -c 0
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
native=${1:-"$root"}
icons=${2:-"$root/icons/default-theme"}
native=$(cd -- "$native" && pwd)
icons=$(cd -- "$icons" && pwd)
run_dir="$root/tests/.custom-actions-ui-run-$$"
trap 'rm -rf "$run_dir"' EXIT
mkdir -p "$run_dir/home" "$run_dir/fixture/other"
export TMPDIR="$run_dir"
mkdir -p "$run_dir/usr/bin" "$run_dir/usr/share/swordfish/icons"
ln -s "$icons" "$run_dir/usr/share/swordfish/icons/default-theme"
ln -s "$native/src/swordfish" "$run_dir/usr/bin/swordfish"
touch "$run_dir/fixture/one 'quoted'.txt" "$run_dir/fixture/two \$HOME.txt"
cat > "$run_dir/fake-terminal" <<'TERMINAL'
#!/bin/sh
test "$1" = "-e" || exit 99
shift
exec "$@" </dev/null
TERMINAL
chmod +x "$run_dir/fake-terminal"
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
    "$root/tests/custom-actions-ui-regression.cpp" "$run_dir/main.o" "${objects[@]}" \
    "$native/st/x.o" "$native/st/st.o" "$native"/libsn/*.o \
    -lutil -lfontconfig -lpng -lFOX-1.6 -lX11 -lfreetype -lXft -lxcb -lxcb-util \
    -lX11-xcb -ldl "${randr_libs[@]}" $(pkg-config --libs gio-2.0) -o "$run_dir/regression"
display_number=$((200 + $$ % 10000))
touch "$run_dir/authority"
xauth -f "$run_dir/authority" add "localhost:$display_number" . "$(mcookie)"
Xvfb ":$display_number" -screen 0 1280x900x24 -nolock -nolisten unix -listen tcp \
    -auth "$run_dir/authority" > "$run_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wm_pid=
cleanup() {
    if [[ -n "$wm_pid" ]]; then kill "$wm_pid" 2>/dev/null || true; wait "$wm_pid" 2>/dev/null || true; fi
    kill "$xvfb_pid" 2>/dev/null || true
    wait "$xvfb_pid" 2>/dev/null || true
    rm -rf "$run_dir"
}
trap cleanup EXIT
for ((waited=0; waited<50; ++waited)); do
    if XAUTHORITY="$run_dir/authority" DISPLAY="localhost:$display_number" xdpyinfo >/dev/null 2>&1; then break; fi
    sleep 0.1
done
XAUTHORITY="$run_dir/authority" DISPLAY="localhost:$display_number" xdpyinfo >/dev/null
# Opt in to a real window manager for maximization checks.
if [[ ${SWORDFISH_TEST_WM:-0} == 1 ]]; then
    HOME="$run_dir/home" XAUTHORITY="$run_dir/authority" DISPLAY="localhost:$display_number" \
        fluxbox -rc /dev/null -no-slit -no-toolbar > "$run_dir/wm.log" 2>&1 &
    wm_pid=$!
    for ((waited=0; waited<50; ++waited)); do
        if XAUTHORITY="$run_dir/authority" DISPLAY="localhost:$display_number" \
            xprop -root _NET_SUPPORTING_WM_CHECK | grep -q 'window id'; then break; fi
        sleep 0.1
    done
    kill -0 "$wm_pid"
    XAUTHORITY="$run_dir/authority" DISPLAY="localhost:$display_number" \
        xprop -root _NET_SUPPORTING_WM_CHECK | grep -q 'window id'
fi
runner=()
if [[ ${SWORDFISH_TEST_DEBUG:-0} == 1 ]]; then
    runner=(gdb --batch --return-child-result -ex run -ex bt --args)
fi
HOME="$run_dir/home" XAUTHORITY="$run_dir/authority" DISPLAY="localhost:$display_number" SWORDFISH_HOST_PATH="/usr/bin:/bin" \
SWORDFISH_HOST_LD_LIBRARY_PATH="" SWORDFISH_HOST_GIO_MODULE_DIR="" \
LD_LIBRARY_PATH="/nonexistent/swordfish-bundle" GIO_MODULE_DIR="/nonexistent/swordfish-modules" \
timeout 90s "${runner[@]}" "$run_dir/regression" "$run_dir/fixture" "$run_dir/usr" "$icons"
