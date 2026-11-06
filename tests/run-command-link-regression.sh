#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
image=${SWORDFISH_TEST_IMAGE:-swordfish-portable-builder:debian-buster-slim}

# All privileged writes are confined to an ephemeral container, never the host.
docker run --rm -i --network none --user 0:0 \
    --volume "$root:/source:ro" "$image" sh -s <<'TEST'
set -eu
g++ -std=c++11 -Wall -Wextra -Werror /source/src/swordfish-command-link.cpp -o /tmp/helper
mkdir -p /home/test /usr/local/bin
printf '#!/bin/sh\nexit 0\n' > /home/test/AppRun
chmod 755 /home/test/AppRun
chown 1000:1000 /home/test/AppRun
export PKEXEC_UID=1000
link=/usr/local/bin/swordfish
target=/home/test/AppRun
record=/var/lib/swordfish-command-links/1000

/tmp/helper create "$link" "$target"
test "$(readlink "$link")" = "$target"
test "$(stat -c %u "$link")" = 0
test "$(stat -c %a "$record")" = 600
/tmp/helper create "$link" "$target"

rm "$link"
ln -s /unrelated "$link"
if /tmp/helper create "$link" "$target"; then exit 1; fi
if /tmp/helper remove "$link" "$target"; then exit 1; fi
test "$(readlink "$link")" = /unrelated
rm "$link"
ln -s "$target" "$link"

PKEXEC_UID=1001 /tmp/helper remove "$link" "$target" && exit 1
test -L "$link"
rm "$target"
/tmp/helper remove "$link" "$target"
test ! -L "$link"
test ! -s "$record"

printf 'unrelated\n' > "$link"
if /tmp/helper create "$link" "$target"; then exit 1; fi
test "$(cat "$link")" = unrelated
if /tmp/helper create /etc/swordfish "$target"; then exit 1; fi
printf 'PASS: isolated privileged helper creates/removes owned links, rejects changes/other users and removes a moved target safely\n'
TEST
