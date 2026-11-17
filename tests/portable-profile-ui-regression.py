#!/usr/bin/python3
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
NATIVE = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else ROOT


def wait_for(predicate, description, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(0.1)
    raise AssertionError(f"Timed out waiting for {description}")


class PortableProfileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.wm = subprocess.Popen(
            ["openbox"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        wait_for(
            lambda: "window id" in subprocess.run(
                ["xprop", "-root", "_NET_SUPPORTING_WM_CHECK"],
                capture_output=True, text=True, check=True).stdout,
            "window manager")

    @classmethod
    def tearDownClass(cls):
        cls.wm.terminate()
        cls.wm.wait(timeout=10)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="swordfish-profile-")
        self.root = Path(self.temporary.name)
        self.home = self.root / "home"
        self.home.mkdir()
        self.env = os.environ.copy()
        for key in list(self.env):
            if key.startswith("SWORDFISH_") or key in ("APPIMAGE", "APPDIR"):
                del self.env[key]
        self.env.update(
            HOME=str(self.home), XDG_CONFIG_HOME=str(self.home / ".config"),
            XDG_DATA_HOME=str(self.home / ".local/share"),
            XDG_CACHE_HOME=str(self.home / ".cache"),
            XDG_STATE_HOME=str(self.home / ".local/state"), LC_ALL="C")
        self.processes = []
        self.restarted = []
        self.main_windows = {}
        self.logs = []

    def tearDown(self):
        for process in self.processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        for pid, handle in self.restarted:
            if handle is not None:
                try:
                    signal.pidfd_send_signal(handle, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                os.close(handle)
            elif self.windows("SwordFish", pid):
                try:
                    os.kill(pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
        for log in self.logs:
            log.close()
        self.temporary.cleanup()

    def bundle(self, name):
        bundle = self.root / name
        binary = bundle / "usr/bin"
        binary.mkdir(parents=True)
        for program in ("swordfish", "swordfish-network"):
            shutil.copy2(NATIVE / "src" / program, binary / program)
        share = bundle / "usr/share/swordfish"
        (share / "icons").mkdir(parents=True)
        (share / "icons/default-theme").symlink_to(ROOT / "icons/default-theme")
        shutil.copy2(ROOT / "swordfishrc", share / "swordfishrc")
        shutil.copy2(ROOT / "portable/AppRun", bundle / "AppRun")
        return bundle

    def profile(self, root, name, marker):
        config = root / "config/swordfish/xferc"
        config.parent.mkdir(parents=True)
        config.write_text(
            "[OPTIONS]\nwelcome_completed=1\n\n"
            "[CUSTOM ACTIONS]\nversion=1\ncount=1\n"
            f"item0.id={name.lower()}\nitem0.parent=\nitem0.name={name} Action\n"
            f"item0.command=/usr/bin/touch {marker}\n"
            "item0.patterns=*\nitem0.kind=0\nitem0.toolbar=1\n"
            "item0.files=1\nitem0.folders=1\nitem0.single=1\nitem0.multiple=1\n"
            "item0.background=0\nitem0.terminal=0\nitem0.confirm=0\n")
        return config

    def windows(self, title, pid=None):
        command = ["xdotool", "search", "--onlyvisible"]
        command += ["--name", "^SwordFish" if title == "SwordFish" else f"^{title}$"]
        result = subprocess.run(command, env=self.env, capture_output=True, text=True)
        found = result.stdout.splitlines() if result.returncode == 0 else []
        if pid is None:
            return found
        main = self.main_windows.get(pid)
        if main is None:
            return []
        if title == "SwordFish":
            return [main] if main in found else []

        def belongs_to_main(window):
            seen = set()
            while window and window != main and window not in seen:
                seen.add(window)
                owner = subprocess.run(
                    ["xprop", "-id", window, "WM_TRANSIENT_FOR"],
                    env=self.env, capture_output=True, text=True)
                match = re.search(r"window id # (0x[0-9a-fA-F]+)", owner.stdout)
                window = str(int(match.group(1), 16)) if match else None
            return window == main
        return [window for window in found if belongs_to_main(window)]

    def window(self, title, pid=None):
        return wait_for(lambda: next(iter(self.windows(title, pid)), None),
                        f"{title} window for {pid}")

    def send(self, window, *keys):
        subprocess.run(["xdotool", "windowactivate", "--sync", window],
                       env=self.env, check=True, timeout=10)
        subprocess.run(["xdotool", "key", "--clearmodifiers", *keys],
                       env=self.env, check=True, timeout=10)

    def start(self, bundle, launcher=None):
        existing = set(self.windows("SwordFish"))
        log = (self.root / f"launch-{len(self.logs)}.log").open("w+")
        self.logs.append(log)
        process = subprocess.Popen(
            [str(launcher or bundle / "AppRun"), str(self.home)],
            env=self.env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        self.processes.append(process)
        def application_window():
            if process.poll() is not None:
                log.flush()
                raise AssertionError(f"Swordfish exited during startup:\n{Path(log.name).read_text()}")
            return next((window for window in self.windows("SwordFish") if window not in existing), None)
        window = wait_for(application_window, "application window")
        self.main_windows[process.pid] = window
        self.assertIsNone(process.poll())
        return process, window

    def choose(self, process, key):
        notice = self.window("Portable settings changed", process.pid)
        self.send(notice, f"alt+{key}")
        wait_for(lambda: not self.windows("Portable settings changed", process.pid),
                 "conflict notice to close")

    def invoke(self, window, marker):
        self.send(window, "alt+a")
        time.sleep(0.2)
        subprocess.run(["xdotool", "key", "--clearmodifiers", "Down", "Return"],
                       env=self.env, check=True, timeout=10)
        wait_for(marker.is_file, "custom action to run")
        marker.unlink()

    def editor(self, window, pid):
        self.send(window, "alt+a")
        time.sleep(0.2)
        subprocess.run(["xdotool", "key", "--clearmodifiers", "Down", "Down", "Return"],
                       env=self.env, check=True, timeout=10)
        return self.window("Custom Actions", pid)

    def normal_quit(self, process, window):
        self.send(window, "ctrl+q")
        self.assertEqual(process.wait(timeout=10), 0)

    def copied_destination(self, appimage=False):
        source = self.bundle("source")
        destination = self.bundle("destination")
        source_marker = self.root / "source-ran"
        destination_marker = self.root / "destination-ran"
        source_data = source / "portable-data"
        launcher = None
        if appimage:
            launcher = self.root / "Swordfish.AppImage"
            launcher.write_text(
                "#!/bin/sh\n"
                "export APPIMAGE=\"$0\"\n"
                f"export APPDIR={shlex.quote(str(destination))}\n"
                "printf 'launch\\n' >> \"$APPIMAGE.launches\"\n"
                "exec \"$APPDIR/AppRun\" \"$@\"\n")
            launcher.chmod(0o755)
            destination_data = Path(str(launcher) + ".data")
        else:
            destination_data = destination / "portable-data"
        source_config = self.profile(source_data, "Source", source_marker)
        destination_config = self.profile(destination_data, "Destination", destination_marker)
        process, window = self.start(destination, launcher)
        shutil.copytree(source_data, destination_data, dirs_exist_ok=True)
        self.assertEqual(destination_config.read_bytes(), source_config.read_bytes())
        return process, window, destination_config, source_marker, destination_marker, launcher

    def new_window_after_restart(self, original):
        def find():
            for window in self.windows("SwordFish"):
                if window in self.main_windows.values():
                    continue
                for entry in Path("/proc").iterdir():
                    if not entry.name.isdigit():
                        continue
                    try:
                        pid = int(entry.name)
                        executable = os.readlink(entry / "exe")
                        if (os.getpgid(pid) == original.pid and
                                executable.startswith(str(self.root) + "/") and
                                Path(executable).name == "swordfish"):
                            return window, pid
                    except (FileNotFoundError, ProcessLookupError, PermissionError):
                        pass
            return None

        self.assertEqual(original.wait(timeout=10), 0)
        self.main_windows.pop(original.pid, None)
        window, pid = wait_for(find, "restarted application")
        self.main_windows[pid] = window
        handle = (os.pidfd_open(pid)
                  if hasattr(os, "pidfd_open") and hasattr(signal, "pidfd_send_signal") else None)
        self.restarted.append((pid, handle))
        return window, pid

    def test_copy_keep_running_and_quit_without_saving(self):
        process, window, config, source_marker, destination_marker, _ = self.copied_destination()
        copied = config.read_bytes()
        self.choose(process, "k")
        self.invoke(window, destination_marker)
        self.assertFalse(source_marker.exists())
        self.send(window, "ctrl+q")
        self.choose(process, "q")
        self.assertEqual(process.wait(timeout=10), 0)
        self.assertEqual(config.read_bytes(), copied)

    def test_quit_immediately_after_copy_keeps_copied_settings(self):
        process, window, config, _, _, _ = self.copied_destination()
        copied = config.read_bytes()
        self.send(window, "ctrl+q")
        self.choose(process, "q")
        self.assertEqual(process.wait(timeout=10), 0)
        self.assertEqual(config.read_bytes(), copied)

    def test_interrupted_save_warns_and_preserves_both_versions(self):
        bundle = self.bundle("interrupted")
        config = self.profile(bundle / "portable-data", "Destination", self.root / "destination-ran")
        saved = config.with_name("xferc_12345")
        saved.write_text("[CUSTOM ACTIONS]\nitem0.name=Recovered Action\n")
        original, backup = config.read_bytes(), saved.read_bytes()
        process, _ = self.start(bundle)
        self.choose(process, "q")
        self.assertEqual(process.wait(timeout=10), 0)
        self.assertEqual(config.read_bytes(), original)
        self.assertEqual(saved.read_bytes(), backup)

    def test_copy_restart_loads_new_profile(self):
        process, _, config, source_marker, _, _ = self.copied_destination()
        self.choose(process, "r")
        window, pid = self.new_window_after_restart(process)
        self.invoke(window, source_marker)
        self.send(window, "ctrl+q")
        wait_for(lambda: not self.windows("SwordFish", pid), "restarted window to close")
        self.assertIn("Source Action", config.read_text())
        self.assertNotIn("Destination Action", config.read_text())

    def test_appimage_style_restart_relaunches_sidecar(self):
        process, _, config, source_marker, _, launcher = self.copied_destination(appimage=True)
        self.choose(process, "r")
        window, pid = self.new_window_after_restart(process)
        self.assertEqual(Path(str(launcher) + ".launches").read_text().splitlines(), ["launch", "launch"])
        self.invoke(window, source_marker)
        self.send(window, "ctrl+q")
        wait_for(lambda: not self.windows("SwordFish", pid), "AppImage-style window to close")
        self.assertIn("Source Action", config.read_text())

    def test_restart_failure_keeps_original_window(self):
        process, window, config, _, _, _ = self.copied_destination()
        copied = config.read_bytes()
        binary = self.root / "destination/usr/bin/swordfish"
        binary.rename(binary.with_name("swordfish-unavailable"))
        self.choose(process, "r")
        error = self.window("Restart failed", process.pid)
        self.send(error, "Return")
        self.assertIsNone(process.poll())
        self.assertEqual(config.read_bytes(), copied)
        self.send(window, "ctrl+q")
        self.choose(process, "q")
        self.assertEqual(process.wait(timeout=10), 0)
        self.assertEqual(config.read_bytes(), copied)

    def test_restart_process_exits_before_window_is_ready(self):
        process, window, config, _, _, _ = self.copied_destination()
        copied = config.read_bytes()
        binary = self.root / "destination/usr/bin/swordfish"
        binary.rename(binary.with_name("swordfish-unavailable"))
        binary.write_text("#!/bin/sh\nexit 42\n")
        binary.chmod(0o755)
        self.choose(process, "r")
        error = self.window("Restart failed", process.pid)
        self.send(error, "Return")
        self.assertIsNone(process.poll())
        self.assertEqual(config.read_bytes(), copied)
        self.send(window, "ctrl+q")
        self.choose(process, "q")
        self.assertEqual(process.wait(timeout=10), 0)
        self.assertEqual(config.read_bytes(), copied)

    def test_another_window_save_is_detected(self):
        bundle = self.bundle("shared")
        config = self.profile(bundle / "portable-data", "Shared", self.root / "shared-ran")
        first, first_window = self.start(bundle)
        initial = config.read_bytes()
        second, second_window = self.start(bundle)
        time.sleep(1.2)
        self.assertEqual(config.read_bytes(), initial)
        self.assertFalse(self.windows("Portable settings changed", first.pid))
        self.assertFalse(self.windows("Portable settings changed", second.pid))
        self.normal_quit(first, first_window)
        saved = config.read_bytes()
        self.choose(second, "k")
        self.send(second_window, "ctrl+q")
        self.choose(second, "q")
        self.assertEqual(second.wait(timeout=10), 0)
        self.assertEqual(config.read_bytes(), saved)

    def test_copy_while_editor_open_blocks_save(self):
        source = self.bundle("source")
        destination = self.bundle("destination")
        self.profile(source / "portable-data", "Source", self.root / "source-ran")
        config = self.profile(destination / "portable-data", "Destination", self.root / "destination-ran")
        process, window = self.start(destination)
        editor = self.editor(window, process.pid)
        shutil.copytree(source / "portable-data", destination / "portable-data", dirs_exist_ok=True)
        copied = config.read_bytes()
        self.choose(process, "k")
        self.send(editor, "Return")
        failure = self.window("Cannot Save Custom Actions", process.pid)
        self.send(failure, "Return")
        self.send(editor, "Escape")
        self.send(window, "ctrl+q")
        self.choose(process, "q")
        self.assertEqual(process.wait(timeout=10), 0)
        self.assertEqual(config.read_bytes(), copied)

    def test_normal_self_saves_do_not_trigger_notice(self):
        bundle = self.bundle("normal")
        config = self.profile(bundle / "portable-data", "Normal", self.root / "normal-ran")
        process, window = self.start(bundle)
        for _ in range(2):
            editor = self.editor(window, process.pid)
            self.send(editor, "Return")
            wait_for(lambda: not self.windows("Custom Actions", process.pid), "editor to close")
            time.sleep(1.2)
            self.assertFalse(self.windows("Portable settings changed", process.pid))
        self.normal_quit(process, window)
        self.assertIn("Normal Action", config.read_text())


if __name__ == "__main__":
    unittest.main(verbosity=2)
