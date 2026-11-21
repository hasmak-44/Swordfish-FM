#!/bin/sh
# Builds swordfish-portable_<version>-<revision>_<arch>.deb from a released
# Swordfish-Portable-<version>[-<arch>].tar.gz. The architecture is taken
# from -a, else from the archive name, else amd64, and must match the bundle.
#
#   packaging/deb-portable/build-deb.sh [-r REVISION] [-o OUTDIR] [-a ARCH] PORTABLE.tar.gz
set -eu

here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
revision=1
arch=
outdir=$(pwd)/build/deb
maintainer=${DEB_MAINTAINER:-"hasmak-44 <336807243+hasmak-44@users.noreply.github.com>"}

while getopts r:o:a: option; do
    case "$option" in
        r) revision=$OPTARG ;;
        o) outdir=$OPTARG ;;
        a) arch=$OPTARG ;;
        *) exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 1 ] || { echo "usage: $0 [-r REVISION] [-o OUTDIR] [-a ARCH] Swordfish-Portable-VERSION[-ARCH].tar.gz" >&2; exit 2; }

tarball=$(realpath -- "$1")
name=$(basename -- "$tarball")
upstream=${name#Swordfish-Portable-}
upstream=${upstream%.tar.gz}
[ "$upstream" != "$name" ] || { echo "not a Swordfish-Portable-VERSION.tar.gz: $name" >&2; exit 1; }
case "$upstream" in
    *-amd64|*-i386|*-armhf|*-arm64)
        [ -n "$arch" ] || arch=${upstream##*-}
        upstream=${upstream%-*}
        ;;
esac
[ -n "$arch" ] || arch=amd64
# ELF class and e_machine of the bundled program for each Debian architecture.
case "$arch" in
    amd64) expected_elf="2 62" ;;
    i386) expected_elf="1 3" ;;
    armhf) expected_elf="1 40" ;;
    arm64) expected_elf="2 183" ;;
    *) echo "unsupported architecture: $arch (use amd64, i386, armhf or arm64)" >&2; exit 1 ;;
esac
# 0.0.3-beta2 must sort before 0.0.3.
version="$(printf '%s' "$upstream" | sed 's/-\(alpha\|beta\|rc\)/~\1/')-$revision"

top=$(tar -tzf "$tarball" | head -n 1 | cut -d/ -f1)
tar -xzOf "$tarball" "$top/AppRun" >/dev/null ||
    { echo "$name has no $top/AppRun" >&2; exit 1; }
actual_elf=$(tar -xzOf "$tarball" "$top/usr/bin/swordfish" | od -An -tu1 -j4 -N1)
actual_elf="$(echo $actual_elf) $(tar -xzOf "$tarball" "$top/usr/bin/swordfish" | od -An -tu2 -j18 -N2)"
actual_elf=$(echo $actual_elf)
[ "$actual_elf" = "$expected_elf" ] ||
    { echo "$name does not contain $arch programs (ELF class/machine $actual_elf)" >&2; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
root=$work/root
mkdir -p "$root/DEBIAN" "$root/usr/lib/swordfish-portable" "$root/usr/share/applications" \
    "$root/usr/share/doc/swordfish-portable"

install -m 0644 "$tarball" "$root/usr/lib/swordfish-portable/Swordfish-Portable.tar.gz"
install -m 0644 "$here/swordfish-portable.desktop" "$root/usr/share/applications/"
install -m 0644 "$here/README.md" "$root/usr/share/doc/swordfish-portable/README.md"

# Menu icons, renamed so they can't clash with a native swordfish package.
tar -xzf "$tarball" -C "$work" "$top/usr/share/icons/hicolor"
for icon in "$work/$top"/usr/share/icons/hicolor/*/apps/swordfish.*; do
    size=$(basename -- "$(dirname -- "$(dirname -- "$icon")")")
    mkdir -p "$root/usr/share/icons/hicolor/$size/apps"
    install -m 0644 "$icon" "$root/usr/share/icons/hicolor/$size/apps/swordfish-portable.${icon##*.}"
done
if [ -f "$here/../../COPYING" ]; then
    install -m 0644 "$here/../../COPYING" "$root/usr/share/doc/swordfish-portable/copyright"
fi

install -m 0755 "$here/debian/postinst" "$here/debian/postrm" "$root/DEBIAN/"
install -m 0644 "$here/debian/templates" "$root/DEBIAN/"
# Includes the unpacked size of the copy made at install time.
copy_kb=$(tar -tvzf "$tarball" | awk '{s += $3} END {print int(s / 1024)}')
size=$(( $(du -sk "$root/usr" | cut -f1) + copy_kb ))
sed -e "s/@VERSION@/$version/" -e "s/@ARCH@/$arch/" -e "s/@SIZE@/$size/" -e "s|@MAINTAINER@|$maintainer|" \
    "$here/debian/control.in" > "$root/DEBIAN/control"

mkdir -p "$outdir"
find "$root" -type d -exec chmod 0755 {} +
deb="$outdir/swordfish-portable_${version}_${arch}.deb"
dpkg-deb --root-owner-group -Zxz --build "$root" "$deb" >/dev/null
echo "$deb"
