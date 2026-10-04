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

echo "[*] Creating Swordfish launcher using existing XFE icons..."

# Use existing XFE binary as Swordfish (reuse, no custom icon needed)
sudo cp -f /usr/bin/xfe /usr/bin/swordfish
sudo chmod +x /usr/bin/swordfish

# Find the XFE desktop installed by make install
XFE_DESKTOP="/usr/share/applications/xfe.desktop"
[ -f "$XFE_DESKTOP" ] || XFE_DESKTOP="/usr/share/applications/org.xfe.xfe.desktop"

if [ -f "$XFE_DESKTOP" ]; then
  sudo cp -f "$XFE_DESKTOP" /usr/share/applications/swordfish.desktop
else
  echo "XFE desktop not found, creating minimal one"
  echo -e "[Desktop Entry]\nName=Swordfish FM\nExec=swordfish\nIcon=xfe\nType=Application\nCategories=Utility;FileManager;" | sudo tee /usr/share/applications/swordfish.desktop > /dev/null
fi

# Force to use existing XFE icons - this is the key fix
sudo sed -i 's/^Icon=.*/Icon=xfe/' /usr/share/applications/swordfish.desktop
sudo sed -i 's/^Name=.*/Name=Swordfish FM/' /usr/share/applications/swordfish.desktop
sudo sed -i 's/^Exec=xfe/Exec=swordfish/' /usr/share/applications/swordfish.desktop
sudo sed -i 's/^Exec=.*xfe.*/Exec=swordfish/' /usr/share/applications/swordfish.desktop

sudo update-desktop-database -q 2>/dev/null || true
sudo gtk-update-icon-cache -f /usr/share/icons/hicolor -q 2>/dev/null || true

echo "=== Install done ==="
echo "Launcher: /usr/share/applications/swordfish.desktop -> Icon=xfe (existing)"
echo "Binary: /usr/bin/swordfish"
ls -l /usr/share/applications/swordfish.desktop /usr/bin/swordfish
grep "^Icon=" /usr/share/applications/swordfish.desktop