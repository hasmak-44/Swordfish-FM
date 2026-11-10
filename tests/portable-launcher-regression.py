#!/usr/bin/python3
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


HOST_KEYS = (
    "PATH", "LD_LIBRARY_PATH", "XDG_CONFIG_HOME", "XDG_DATA_HOME",
    "XDG_CONFIG_DIRS", "XDG_DATA_DIRS", "GIO_MODULE_DIR",
)


class PortableLauncherTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="swordfish-launcher-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bundle = self.root / "new bundle $test"
        binary = self.bundle / "usr/bin"
        binary.mkdir(parents=True)
        shutil.copyfile(
            Path(__file__).resolve().parents[1] / "portable/AppRun", self.bundle / "AppRun")
        (self.bundle / "AppRun").chmod(0o755)
        keys = ["SWORDFISH_HOST_" + key for key in HOST_KEYS]
        keys += ["SWORDFISH_PORTABLE_DATA", "XDG_CONFIG_HOME", "GIO_MODULE_DIR", "GIO_USE_VFS"]
        (binary / "swordfish").write_text(
            "#!/usr/bin/python3\nimport json, os, sys\n"
            f"keys = {keys!r}\n"
            "print(json.dumps({'environment': {key: os.environ.get(key) for key in keys}, "
            "'arguments': sys.argv[1:]}))\n")
        (binary / "swordfish").chmod(0o755)
        (binary / "swfa").symlink_to("swordfish")
        (self.bundle / "run-swfa").symlink_to("AppRun")
        (self.bundle / "usr/lib/swordfish/pixbuf-loaders").mkdir(parents=True)
        (binary / "gdk-pixbuf-query-loaders").write_text("#!/bin/sh\nprintf '# fixture loaders\\n'\n")
        (binary / "gdk-pixbuf-query-loaders").chmod(0o755)
        self.env = {"PATH": "/usr/bin:/bin", "HOME": str(self.root / "home"), "LC_ALL": "C"}
        self.expected = {
            "PATH": self.env["PATH"], "LD_LIBRARY_PATH": "",
            "XDG_CONFIG_HOME": self.env["HOME"] + "/.config",
            "XDG_DATA_HOME": self.env["HOME"] + "/.local/share",
            "XDG_CONFIG_DIRS": "/etc/xdg", "XDG_DATA_DIRS": "/usr/local/share:/usr/share",
            "GIO_MODULE_DIR": "",
        }

    def launch(self, env, program="AppRun"):
        result = subprocess.run(
            [str(self.bundle / program), "argument with spaces"],
            env=env, capture_output=True, text=True, check=True, timeout=10)
        return json.loads(result.stdout)

    def check_host(self, result, expected):
        for key, value in expected.items():
            self.assertEqual(result["environment"]["SWORDFISH_HOST_" + key], value, key)
        self.assertEqual(result["arguments"], ["argument with spaces"])
        self.assertIsNone(result["environment"]["GIO_USE_VFS"])

    def test_normal_launch_records_host_defaults(self):
        result = self.launch(self.env)
        self.check_host(result, self.expected)
        self.assertEqual(
            result["environment"]["SWORDFISH_PORTABLE_DATA"], str(self.bundle / "portable-data"))

    def test_normal_launch_records_custom_host_environment(self):
        expected = dict(self.expected)
        for key in HOST_KEYS:
            if key != "PATH":
                expected[key] = str(self.root / ("host " + key))
        env = dict(self.env, **expected)
        self.check_host(self.launch(env), expected)

    def test_nested_launch_preserves_original_host_environment(self):
        expected = dict(self.expected)
        expected["LD_LIBRARY_PATH"] = str(self.root / "host libraries")
        expected["GIO_MODULE_DIR"] = str(self.root / "host gio modules")
        env = dict(self.env)
        for key, value in expected.items():
            env["SWORDFISH_HOST_" + key] = value
            env[key] = str(self.root / "old bundle" / key)
        env["PATH"] += ":/usr/bin:/bin"
        result = self.launch(env)
        self.check_host(result, expected)
        self.assertEqual(
            result["environment"]["XDG_CONFIG_HOME"], str(self.bundle / "portable-data/config"))
        self.assertEqual(
            result["environment"]["GIO_MODULE_DIR"], str(self.bundle / "usr/lib/swordfish/gio-modules"))

    def test_nested_launch_preserves_empty_host_library_and_gio_settings(self):
        env = dict(self.env)
        for key, value in self.expected.items():
            env["SWORDFISH_HOST_" + key] = value
        env["LD_LIBRARY_PATH"] = str(self.root / "old bundle/usr/lib")
        env["GIO_MODULE_DIR"] = str(self.root / "old bundle/usr/lib/swordfish/gio-modules")
        self.check_host(self.launch(env), self.expected)

    def test_two_portable_launchers_preserve_host_environment(self):
        outer = self.root / "outer bundle"
        binary = outer / "usr/bin"
        binary.mkdir(parents=True)
        shutil.copyfile(self.bundle / "AppRun", outer / "AppRun")
        (outer / "AppRun").chmod(0o755)
        (binary / "swordfish").write_text('#!/bin/sh\nexec "$NEXT_APP" "$@"\n')
        (binary / "swordfish").chmod(0o755)
        (outer / "usr/lib/swordfish/pixbuf-loaders").mkdir(parents=True)
        shutil.copyfile(
            self.bundle / "usr/bin/gdk-pixbuf-query-loaders", binary / "gdk-pixbuf-query-loaders")
        (binary / "gdk-pixbuf-query-loaders").chmod(0o755)
        env = dict(self.env, NEXT_APP=str(self.bundle / "AppRun"))
        result = subprocess.run(
            [str(outer / "AppRun"), "argument with spaces"],
            env=env, capture_output=True, text=True, check=True, timeout=10)
        self.check_host(json.loads(result.stdout), self.expected)

    def test_appimage_profile_and_companion_launcher(self):
        env = dict(self.env, APPDIR=str(self.bundle), APPIMAGE=str(self.root / "test.AppImage"))
        result = self.launch(env, "run-swfa")
        self.check_host(result, self.expected)
        self.assertEqual(
            result["environment"]["SWORDFISH_PORTABLE_DATA"], str(self.root / "test.AppImage.data"))
        self.assertTrue((self.root / "test.AppImage.data/cache/pixbuf-loaders.cache").is_file())


if __name__ == "__main__":
    unittest.main()
