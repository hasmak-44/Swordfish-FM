#!/usr/bin/python3
from pathlib import Path
import subprocess
import tempfile
import unittest


class IconLinksTests(unittest.TestCase):
    def test_installed_fallback_links_have_targets_and_preserve_theme_icons(self):
        with tempfile.TemporaryDirectory(prefix="swordfish-icon-links-") as directory:
            root = Path(directory)
            source = root / "source"
            destination = root / "installed"
            for name in ("default-theme", "apps/48x48/apps", "gnome-theme"):
                (source / "icons" / name).mkdir(parents=True)
            (source / "icons/default-theme/file.png").write_bytes(b"default")
            (source / "icons/apps/48x48/apps/swordfish.png").write_bytes(b"application")
            (source / "icons/gnome-theme/file.png").write_bytes(b"custom")
            theme = destination / "icons/gnome-theme"
            theme.mkdir(parents=True)
            (theme / "file.png").write_bytes(b"custom")
            (theme / "swordfish.png").symlink_to("../default-theme/swordfish.png")
            command = [
                "bash", str(Path(__file__).resolve().parents[1] / "iconlinks.sh"),
                str(source), str(destination),
            ]
            for _ in range(2):
                subprocess.run(command, check=True, capture_output=True, text=True)
                self.assertEqual((theme / "file.png").read_bytes(), b"custom")
                for name in ("gnome-theme", "kde-theme", "xfce-theme"):
                    self.assertEqual(
                        (destination / "icons" / name / "swordfish.png").read_bytes(),
                        b"application")
                    expected = b"custom" if name == "gnome-theme" else b"default"
                    self.assertEqual(
                        (destination / "icons" / name / "file.png").read_bytes(), expected)
                for link in destination.rglob("*"):
                    if link.is_symlink():
                        self.assertTrue(link.is_file(), str(link))


if __name__ == "__main__":
    unittest.main()
