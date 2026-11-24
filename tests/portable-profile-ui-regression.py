#!/usr/bin/python3
import configparser
import fcntl
import hashlib
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
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
            "item0.background=1\nitem0.terminal=0\nitem0.confirm=0\n")
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

    def start(self, bundle, launcher=None, arguments=None):
        existing = set(self.windows("SwordFish"))
        log = (self.root / f"launch-{len(self.logs)}.log").open("w+")
        self.logs.append(log)
        if arguments is None:
            arguments = [str(self.home)]
        process = subprocess.Popen(
            [str(launcher or bundle / "AppRun"), *arguments],
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
        self.main_windows.pop(process.pid, None)

    def publish_peer_setting(self, config, section, key, value):
        lock = os.open(config.with_name(config.name + ".lock"), os.O_RDONLY | os.O_CREAT, 0o444)
        try:
            fcntl.flock(lock, fcntl.LOCK_EX)
            settings = configparser.ConfigParser(interpolation=None)
            settings.optionxform = str
            with config.open() as stream:
                settings.read_file(stream)
            settings[section][key] = str(value)
            with tempfile.NamedTemporaryFile(
                    mode="w", encoding="utf-8", dir=config.parent, prefix="peer-settings-", delete=False) as stream:
                settings.write(stream)
                temporary = Path(stream.name)
            os.replace(temporary, config)
            digest = hashlib.sha256(config.read_bytes()).digest()
            info = config.stat()
            fields = (info.st_dev, info.st_ino, info.st_mode, info.st_uid, info.st_gid, info.st_size,
                      info.st_mtime_ns // 10**9, info.st_mtime_ns % 10**9,
                      info.st_ctime_ns // 10**9, info.st_ctime_ns % 10**9)
            metadata = (":".join(str(field) for field in fields) + ":").encode()
            record = hashlib.sha256(metadata + digest).hexdigest() + "\n"
            with tempfile.NamedTemporaryFile(
                    mode="w", dir=config.parent, prefix="peer-record-", delete=False) as stream:
                stream.write(record)
                temporary = Path(stream.name)
            temporary.chmod(0o444)
            os.replace(temporary, config.with_name(config.name + ".saved"))
        finally:
            fcntl.flock(lock, fcntl.LOCK_UN)
            os.close(lock)

    def replace_profile(self, source, destination):
        incoming = destination.with_name(destination.name + "-incoming")
        previous = destination.with_name(destination.name + "-previous")
        shutil.copytree(source, incoming)
        destination.rename(previous)
        incoming.rename(destination)

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
        source_process, source_window = self.start(source)
        self.normal_quit(source_process, source_window)
        destination_config = self.profile(destination_data, "Destination", destination_marker)
        process, window = self.start(destination, launcher)
        self.replace_profile(source_data, destination_data)
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
        time.sleep(2)
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

    def test_another_window_save_synchronizes_without_conflict(self):
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
        time.sleep(1.2)
        self.assertFalse(self.windows("Portable settings changed", second.pid))
        self.send(second_window, "ctrl+q")
        self.assertEqual(second.wait(timeout=10), 0)
        self.assertTrue(config.exists())

    def test_start_during_peer_save_does_not_report_interrupted_save(self):
        bundle = self.bundle("starting-during-save")
        config = self.profile(bundle / "portable-data", "Shared", self.root / "shared-ran")
        process, window = self.start(bundle)
        self.normal_quit(process, window)
        ready = threading.Event()

        def saving():
            with config.with_name(config.name + ".lock").open("rb") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                temporary = config.with_name(f"xferc_{os.getpid()}")
                temporary.write_bytes(config.read_bytes())
                ready.set()
                time.sleep(3)
                temporary.unlink()

        writer = threading.Thread(target=saving)
        writer.start()
        try:
            self.assertTrue(ready.wait(5))
            process, window = self.start(bundle)
            writer.join(timeout=10)
            time.sleep(1.2)
            self.assertFalse(self.windows("Portable settings changed", process.pid))
            self.normal_quit(process, window)
        finally:
            writer.join(timeout=10)

    @unittest.skipUnless(os.environ.get("SWORDFISH_TEST_ROOT") == "1",
                         "requires explicit opt-in to a sudo root-window check")
    def test_root_window_save_keeps_normal_window_writable(self):
        bundle = self.bundle("root-shared")
        config = self.profile(bundle / "portable-data", "Shared", self.root / "shared-ran")
        config.write_text(config.read_text().replace("welcome_completed=1", "welcome_completed=1\nroot_warn=0"))
        config.chmod(0o600)
        first, first_window = self.start(bundle)
        launcher = self.root / "root-launcher"
        environment = {key: self.env[key] for key in
                       ("HOME", "DISPLAY", "XDG_CONFIG_HOME", "XDG_DATA_HOME",
                        "XDG_CACHE_HOME", "XDG_STATE_HOME", "LC_ALL")}
        if "XAUTHORITY" in self.env:
            environment["XAUTHORITY"] = self.env["XAUTHORITY"]
        command = ["sudo", "-A", "env", *[f"{key}={value}" for key, value in environment.items()],
                   str(bundle / "AppRun")]
        launcher.write_text("#!/bin/sh\numask 077\nexec " + shlex.join(command) + ' "$@"\n')
        launcher.chmod(0o755)
        try:
            second, second_window = self.start(bundle, launcher)
            self.normal_quit(second, second_window)
            self.assertEqual(config.stat().st_uid, os.getuid())
            self.assertEqual(config.stat().st_mode & 0o777, 0o600)
            self.assertEqual(config.with_name(config.name + ".saved").stat().st_mode & 0o777, 0o444)
            time.sleep(1.2)
            self.assertFalse(self.windows("Portable settings changed", first.pid))
            self.normal_quit(first, first_window)
            self.assertEqual(config.stat().st_uid, os.getuid())
        finally:
            for process in self.processes:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
            subprocess.run(["sudo", "-A", "chown", "-R", f"{os.getuid()}:{os.getgid()}",
                            str(bundle)], check=True)

    def test_peer_preference_updates_merge_in_open_windows(self):
        bundle = self.bundle("shared")
        config = self.profile(bundle / "portable-data", "Shared", self.root / "shared-ran")
        first, first_window = self.start(bundle)
        second, second_window = self.start(bundle)
        self.publish_peer_setting(config, "OPTIONS", "log_level", 1)
        time.sleep(1.2)
        self.assertFalse(self.windows("Portable settings changed", first.pid))
        self.assertFalse(self.windows("Portable settings changed", second.pid))
        self.publish_peer_setting(config, "OPTIONS", "log_level", 4)
        time.sleep(1.2)
        self.assertFalse(self.windows("Portable settings changed", first.pid))
        self.assertFalse(self.windows("Portable settings changed", second.pid))
        self.normal_quit(first, first_window)
        time.sleep(1.2)
        self.assertFalse(self.windows("Portable settings changed", second.pid))
        self.normal_quit(second, second_window)
        settings = configparser.ConfigParser(interpolation=None)
        with config.open() as stream:
            settings.read_file(stream)
        self.assertEqual(settings.getint("OPTIONS", "log_level"), 4)

    def test_copy_while_editor_open_blocks_save(self):
        source = self.bundle("source")
        destination = self.bundle("destination")
        self.profile(source / "portable-data", "Source", self.root / "source-ran")
        source_process, source_window = self.start(source)
        self.normal_quit(source_process, source_window)
        config = self.profile(destination / "portable-data", "Destination", self.root / "destination-ran")
        process, window = self.start(destination)
        editor = self.editor(window, process.pid)
        self.replace_profile(source / "portable-data", destination / "portable-data")
        copied = config.read_bytes()
        action = destination / "portable-data/custom-actions/Source Action/action.ini"
        copied_action = action.read_bytes()
        self.choose(process, "k")
        self.send(editor, "Return")
        failure = self.window("Cannot Save Custom Actions", process.pid)
        self.send(failure, "Return")
        self.send(editor, "Escape")
        self.send(window, "ctrl+q")
        self.choose(process, "q")
        self.assertEqual(process.wait(timeout=10), 0)
        self.assertEqual(config.read_bytes(), copied)
        self.assertEqual(action.read_bytes(), copied_action)

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

    def test_selected_icon_theme_moves_with_profile(self):
        source = self.bundle("source bundle")
        destination = self.bundle("destination bundle")
        source_data = source / "portable-data"
        theme = source_data / "icons/Custom theme-system"
        shutil.copytree(ROOT / "icons/default-theme", theme)
        config = self.profile(source_data, "Source", self.root / "source-ran")
        config.write_text(f"[SETTINGS]\niconpath={theme}\n\n" + config.read_text())
        process, window = self.start(source)
        self.normal_quit(process, window)
        self.assertIn("iconpath=portable:icons/Custom theme-system", config.read_text())

        destination_data = destination / "portable-data"
        shutil.copytree(source_data, destination_data)
        (theme / "bigfolder.png").unlink()
        process, window = self.start(destination)
        self.normal_quit(process, window)
        moved_config = destination_data / "config/swordfish/xferc"
        self.assertIn("iconpath=portable:icons/Custom theme-system", moved_config.read_text())

    def test_first_launch_has_no_bundled_user_content(self):
        bundle = self.bundle("empty profile")
        data = bundle / "portable-data"
        config = data / "config/swordfish/xferc"
        config.parent.mkdir(parents=True)
        config.write_text("[OPTIONS]\nwelcome_completed=1\n")
        process, window = self.start(bundle)
        self.normal_quit(process, window)
        for directory in ("themes/colors", "textures", "patterns", "images", "scripts",
                          "icons/custom-actions", "addons"):
            with self.subTest(directory=directory):
                self.assertEqual(list((data / directory).iterdir()), [])
        actions = data / "custom-actions"
        bookkeeping = {".swordfish-storage", ".swordfish-known-actions"}
        self.assertEqual({entry.name for entry in actions.iterdir()} - bookkeeping, set())

    def test_action_scripts_stay_external_and_manual_edits_survive_restart(self):
        source = self.bundle("script source")
        destination = self.bundle("script destination")
        source_data = source / "portable-data"
        marker = self.root / "script-ran"
        script_dir = self.root / "user scripts"
        script_dir.mkdir()
        script = script_dir / "external-script.sh"
        script.write_text(f"#!/bin/sh\n. ./helper.sh\n/usr/bin/touch {shlex.quote(str(marker))}\n")
        (script_dir / "helper.sh").write_text(":\n")
        script.chmod(0o600)
        config = self.profile(source_data, "Script", marker)
        config.write_text(config.read_text().replace(
            f"item0.command=/usr/bin/touch {marker}",
            f"item0.command=cd {shlex.quote(str(script_dir))} && /bin/sh ./{script.name}"))

        process, window = self.start(source)
        self.invoke(window, marker)
        self.normal_quit(process, window)
        action = source_data / "custom-actions/Script Action/action.ini"
        self.assertNotIn("%a/", action.read_text())
        self.assertFalse((action.parent / script.name).exists())

        edited = re.sub(r"(?m)^Command=.*$", f"Command=/bin/sh {shlex.quote(str(script))}",
                        action.read_text()) + "\nUserField=kept\n"
        action.write_text(edited)
        process, window = self.start(source)
        self.normal_quit(process, window)
        self.assertEqual(action.read_text(), edited)
        self.assertFalse((action.parent / script.name).exists())
        self.assertEqual([p.name for p in action.parent.glob(".action.ini-before-*")], [])

        destination_data = destination / "portable-data"
        shutil.copytree(source_data, destination_data)
        shutil.rmtree(source)
        process, window = self.start(destination)
        self.normal_quit(process, window)
        self.assertEqual((destination_data / "custom-actions/Script Action/action.ini").read_text(), edited)

    def test_bookmarks_and_association_icons_move_with_profile(self):
        source = self.bundle("icon source")
        destination = self.bundle("icon destination")
        source_data = source / "portable-data"
        icon = source_data / "images/bookmark.png"
        icon.parent.mkdir(parents=True)
        shutil.copy2(ROOT / "icons/default-theme/minibookmark.png", icon)
        config = self.profile(source_data, "Icons", self.root / "icons-ran")
        initial = config.read_text().replace("welcome_completed=1\n",
                                             "welcome_completed=1\nlegacy_bookmarks=0\n")
        config.write_text(initial +
            f"\n[BOOKMARKS]\nbookmark0=Inside;{icon.parent};{icon}\n"
            f"bookmark1=Home;{self.home};{icon}\n"
            f"\n[FILETYPES]\nicon-test=open;Example;{icon};{icon};;text/example\n")

        process, window = self.start(source)
        self.normal_quit(process, window)
        saved = config.read_text()
        self.assertIn("bookmark0=Inside;@portable/images;@portable/images/bookmark.png", saved)
        self.assertIn(f"bookmark1=Home;{self.home};@portable/images/bookmark.png", saved)
        self.assertRegex(saved, r"(?m)^icon-test=.*@portable/images/bookmark\.png")

        destination_data = destination / "portable-data"
        shutil.copytree(source_data, destination_data)
        icon.unlink()
        process, window = self.start(destination)
        self.normal_quit(process, window)
        moved = (destination_data / "config/swordfish/xferc").read_text()
        self.assertIn("bookmark0=Inside;@portable/images;@portable/images/bookmark.png", moved)
        self.assertIn(f"bookmark1=Home;{self.home};@portable/images/bookmark.png", moved)
        self.assertRegex(moved, r"(?m)^icon-test=.*@portable/images/bookmark\.png")

    def test_saved_locations_migrate_after_copy_before_upgrade(self):
        source = self.bundle("location source")
        destination = self.bundle("location destination")
        source_data = source / "portable-data"
        documents = source_data / "documents"
        documents.mkdir(parents=True)
        icon = documents / "bookmark.png"
        shutil.copy2(ROOT / "icons/default-theme/minibookmark.png", icon)
        config = source_data / "config/swordfish/xferc"
        config.parent.mkdir(parents=True)
        config.write_text(
            "[OPTIONS]\nwelcome_completed=1\nlegacy_bookmarks=0\n"
            "startdir_mode=2\nrestore_tabs=1\n"
            f"\n[LEFT PANEL]\nlastdir={documents}\n"
            f"\n[RIGHT PANEL]\nlastdir={source_data}\n"
            f"\n[LEFT TABS]\ntab0={documents}\ncurrent_tab=0\n"
            f"\n[RIGHT TABS]\ntab0={source_data}\ncurrent_tab=0\n"
            f"\n[BOOKMARKS]\nbookmark0=Inside;{documents};{icon}\n"
        )
        destination_data = destination / "portable-data"
        shutil.copytree(source_data, destination_data)
        source_data.rename(source / "old profile")

        capture = self.root / "new-window-arguments"
        self.env["SWORDFISH_TEST_CAPTURE"] = str(capture)
        process, window = self.start(destination, arguments=[])
        expected = str(destination_data / "documents")
        wait_for(
            lambda: expected in subprocess.run(
                ["xdotool", "getwindowname", window], env=self.env,
                capture_output=True, text=True, check=True).stdout,
            "relocated startup folder")
        binary = destination / "usr/bin/swordfish"
        original = binary.with_name("swordfish-original")
        binary.rename(original)
        try:
            binary.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$SWORDFISH_TEST_CAPTURE"\n')
            binary.chmod(0o755)
            self.send(window, "F3")
            wait_for(capture.is_file, "new window command")
            self.assertEqual(capture.read_text(), expected + "\n")
        finally:
            if binary.exists():
                binary.unlink()
            original.rename(binary)
        self.normal_quit(process, window)
        saved = (destination_data / "config/swordfish/xferc").read_text()
        self.assertIn("bookmark0=Inside;@portable/documents;@portable/documents/bookmark.png", saved)
        self.assertIn("lastdir=@portable/documents", saved)
        self.assertIn("tab0=@portable/documents", saved)
        self.assertIn("lastdir=@portable/", saved)

    def test_legacy_icon_theme_path_migrates_after_profile_copy(self):
        source = self.bundle("legacy source")
        destination = self.bundle("legacy destination")
        source_data = source / "portable-data"
        theme = source_data / "themes/Manual theme"
        shutil.copytree(ROOT / "icons/default-theme", theme)
        config = self.profile(source_data, "Legacy", self.root / "legacy-ran")
        config.write_text(f"[SETTINGS]\niconpath={theme}\n\n" + config.read_text())

        destination_data = destination / "portable-data"
        shutil.copytree(source_data, destination_data)
        (theme / "bigfolder.png").unlink()
        process, window = self.start(destination)
        self.normal_quit(process, window)
        moved_config = destination_data / "config/swordfish/xferc"
        self.assertIn("iconpath=portable:themes/Manual theme", moved_config.read_text())

    def test_selected_bundled_icon_theme_moves_with_profile(self):
        source = self.bundle("bundled source")
        destination = self.bundle("bundled destination")
        for bundle in (source, destination):
            shutil.copytree(ROOT / "icons/default-theme", bundle / "usr/share/swordfish/icons/Bundled theme")
        source_data = source / "portable-data"
        config = self.profile(source_data, "Bundled", self.root / "bundled-ran")
        theme = source / "usr/share/swordfish/icons/Bundled theme"
        config.write_text(f"[SETTINGS]\niconpath={theme}\n\n" + config.read_text())
        process, window = self.start(source)
        self.normal_quit(process, window)
        self.assertIn("iconpath=bundle:icons/Bundled theme", config.read_text())

        destination_data = destination / "portable-data"
        shutil.copytree(source_data, destination_data)
        (theme / "bigfolder.png").unlink()
        process, window = self.start(destination)
        self.normal_quit(process, window)
        moved_config = destination_data / "config/swordfish/xferc"
        self.assertIn("iconpath=bundle:icons/Bundled theme", moved_config.read_text())


if __name__ == "__main__":
    unittest.main(verbosity=2)
