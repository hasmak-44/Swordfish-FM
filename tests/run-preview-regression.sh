#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
scratch=$(mktemp -d /tmp/swordfish-preview-test.XXXXXX)
trap 'rm -f "$scratch/preview-test" "$scratch/swordfish-preview"; find "$scratch/fixtures" "$scratch/cache" "$scratch/state" -type f -delete 2>/dev/null; find "$scratch" -depth -type d -empty -delete' EXIT
mkdir -p "$scratch/fixtures"
cd "$root"
make -C src -j2 swordfish swordfish-preview
g++ -std=gnu++11 -Isrc $(fox-config --cflags) \
    $(pkg-config --cflags gdk-pixbuf-2.0 cairo libwebp) \
    tests/preview-regression.cpp src/Preview.o src/AppLogger.o \
    $(pkg-config --libs gio-2.0 gdk-pixbuf-2.0 cairo libwebp) $(fox-config --libs) \
    -o "$scratch/preview-test"
cat > "$scratch/swordfish-preview" <<EOF
#!/bin/sh
case "\$1" in
    */slow.png) exec /bin/sleep 30 ;;
esac
exec "${PREVIEW_TEST_WORKER:-$root/src/swordfish-preview}" "\$@"
EOF
chmod 755 "$scratch/swordfish-preview"
XDG_CACHE_HOME="$scratch/cache" XDG_STATE_HOME="$scratch/state" \
    timeout 45s xvfb-run -a "$scratch/preview-test" "$scratch/fixtures"
