#!/bin/sh
set -eu

if [ "$#" -ne 1 ] || [ ! -f "$1" ]; then
    echo "usage: $0 /path/to/swordfish-portable.deb" >&2
    exit 2
fi

deb=$(realpath -- "$1")
case "$(dpkg-deb -f "$deb" Architecture)" in
    i386) platform=linux/386 ;;
    armhf) platform=linux/arm/v7 ;;
    arm64) platform=linux/arm64 ;;
    *) platform=linux/amd64 ;;
esac
docker run --rm --network none --platform "$platform" \
    --mount "type=bind,src=$deb,dst=/tmp/swordfish-portable.deb,readonly" \
    debian:bookworm-slim sh -eu -c '
        useradd -m tester
        parent=/home/tester/Applications
        existing=$parent/.Swordfish-Portable.installing
        target=$parent/Swordfish-Portable
        link=/usr/local/bin/swordfish-portable

        mkdir -p "$existing"
        printf "%s\n" "existing user data" > "$existing/user-data"
        chown -R tester:tester "$parent"

        SUDO_USER=tester DEBIAN_FRONTEND=noninteractive dpkg -i /tmp/swordfish-portable.deb
        if ! grep -Fxq "existing user data" "$existing/user-data"; then
            echo "FAIL: installation removed an existing staging folder" >&2
            exit 1
        fi
        if [ ! -x "$target/AppRun" ] || [ ! -L "$link" ] ||
           [ "$(readlink "$link")" != "$target/AppRun" ]; then
            echo "FAIL: installation did not create the portable app and command" >&2
            exit 1
        fi

        install -d -o tester -g tester "$target/portable-data"
        printf "%s\n" "saved settings" > "$target/portable-data/settings"
        chown tester:tester "$target/portable-data/settings"
        SUDO_USER=tester DEBIAN_FRONTEND=noninteractive dpkg -i /tmp/swordfish-portable.deb
        if ! grep -Fxq "saved settings" "$target/portable-data/settings" ||
           ! grep -Fxq "existing user data" "$existing/user-data"; then
            echo "FAIL: reinstall did not preserve settings and existing staging data" >&2
            exit 1
        fi

        DEBIAN_FRONTEND=noninteractive dpkg -r swordfish-portable
        if [ -e "$target" ] || [ -L "$link" ] ||
           ! grep -Fxq "existing user data" "$existing/user-data"; then
            echo "FAIL: removal affected the wrong files" >&2
            exit 1
        fi
        echo "PASS: install, reinstall and removal preserve existing staging data"
    '
