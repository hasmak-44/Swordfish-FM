#!/bin/bash
set -e
echo "=== Swordfish-FM install (original XFE graphics) ==="
if [ ! -f configure ]; then
  echo "Running autogen..."
  ./autogen.sh
fi
./configure --prefix=/usr
make -j$(nproc)
echo "Installing..."
sudo make install
echo "=== Install done ==="
echo "Launcher: /usr/share/applications/swordfish.desktop"
echo "Binary: /usr/bin/swordfish"
ls -l /usr/share/applications/swordfish.desktop /usr/bin/swordfish || true
