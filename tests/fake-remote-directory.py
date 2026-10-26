#!/usr/bin/env python3
import base64
import os
import stat
import sys
import time
from urllib.parse import unquote, urlsplit

if os.path.basename(sys.argv[0]) == "udisksctl":
    assert not os.environ.get("LD_LIBRARY_PATH")
    assert not os.environ.get("GIO_MODULE_DIR")
    action = sys.argv[1]
    assert sys.argv[2:] == ["-b", "/dev/test-usb1"]
    with open(os.environ["SWORDFISH_TEST_MOUNT_LOG"], "a") as log:
        log.write("disk " + action + "\n")
    if action == "unmount" and os.environ.get("SWORDFISH_TEST_DISK_BUSY"):
        print("org.freedesktop.UDisks2.Error.DeviceBusy: target is busy")
        sys.exit(1)
    busy_once = os.environ.get("SWORDFISH_TEST_DISK_BUSY_ONCE")
    if action == "unmount" and busy_once and os.path.exists(busy_once):
        os.unlink(busy_once)
        print("org.freedesktop.UDisks2.Error.DeviceBusy: target is busy")
        sys.exit(1)
    if action in ("mount", "unmount"):
        with open(os.environ["SWORDFISH_TEST_DISK_MTAB"], "w") as table:
            table.write("/dev/root / ext4 rw 0 0\n")
            if action == "mount":
                table.write("/dev/test-usb1 " + os.environ["SWORDFISH_TEST_DISK_PATH"] + " ext4 rw 0 0\n")
    else:
        assert action == "power-off"
    sys.exit(0)

if os.path.basename(sys.argv[0]) == "lsblk":
    assert not ("-l" in sys.argv and "-P" in sys.argv), "lsblk --list and --pairs are mutually exclusive"
    assert not os.environ.get("LD_LIBRARY_PATH")
    assert not os.environ.get("GIO_MODULE_DIR")
    if "HOTPLUG" in sys.argv:
        print("1" if sys.argv[-1] == "/dev/test-usb1" else "0")
    elif os.environ.get("SWORDFISH_TEST_VOLUMES"):
        with open(os.environ["SWORDFISH_TEST_VOLUMES"]) as volumes:
            print(volumes.read(), end="")
    sys.exit(0)

if os.path.basename(sys.argv[0]) == "xdg-open":
    assert not os.environ.get("LD_LIBRARY_PATH")
    assert not os.environ.get("GIO_MODULE_DIR")
    assert sys.argv[1:] == ["https://github.com/hasmak-44/Swordfish-FM"]
    with open(os.environ["SWORDFISH_TEST_MOUNT_LOG"], "a") as log:
        log.write("website " + sys.argv[1] + "\n")
    sys.exit(0)

if os.path.basename(sys.argv[0]) == "gio":
    assert not os.environ.get("LD_LIBRARY_PATH")
    assert not os.environ.get("GIO_MODULE_DIR")
    state = os.environ["SWORDFISH_TEST_MOUNT_LOG"]
    gvfs = os.environ.get("SWORDFISH_TEST_GVFS_ROOT")
    if gvfs and sys.argv[1] == "info":
        # A mount registered under a different login URI isn't resolved by gio info.
        print("gio: location is not mounted")
        sys.exit(1)
    if sys.argv[1:3] == ["mount", "-u"]:
        with open(state, "a") as log:
            log.write("unmount " + sys.argv[-1] + "\n")
        if os.environ.get("SWORDFISH_TEST_UNMOUNT_FAIL"):
            print("gio: share is busy")
            sys.exit(1)
        sys.exit(0)
    if urlsplit(sys.argv[-1]).hostname == "hang.test":
        # Mimic gio asking for credentials again and again on a closed stdin
        if sys.argv[1] == "mount":
            time.sleep(600)
        print("gio: location is not mounted")
        sys.exit(1)
    if sys.argv[1] == "mount":
        with open(state, "a") as log:
            log.write(sys.argv[-1] + "\n")
        if gvfs:
            login = unquote(urlsplit(sys.argv[-1]).username or "guest")
            domain, user = login.split(";", 1) if ";" in login else ("", login)
            name = f"smb-share:user={user},share=Team%20files,server=reuse.test"
            if domain:
                name += ",domain=" + domain
            os.mkdir(os.path.join(gvfs, name))
    elif sys.argv[1] == "info":
        assert os.environ.get("LC_ALL") == "C", "Mount discovery must not parse localized output"
        if os.path.exists(state):
            print("local path: " + os.environ["SWORDFISH_TEST_MOUNT_PATH"])
        else:
            print("gio: location is not mounted")
    sys.exit(0)


def entry(name, mode=stat.S_IFDIR | 0o755, target="", symlink=False):
    encoded = base64.b64encode(name.encode()).decode()
    link = base64.b64encode(target.encode()).decode()
    print(
        f"('{encoded}', uint32 {mode}, uint32 {os.getuid()}, uint32 {os.getgid()}, "
        f"int64 1, int64 1, int64 0, '{link}', {'true' if symlink else 'false'}, 1, 1)"
    )


if sys.argv[1:] == ["--discover"]:
    records = [
        "SERVER\t192.168.1.106\tmx-ryzen.local\tMX-RYZEN\t\tgvfs\t1",
        "SERVER\t192.168.1.104\t192.168.1.104\t192.168.1.104\t\tws-discovery\t0",
        "SERVER\t192.168.1.102\tmx-hp.local\tMX-HP\t\tgvfs,ws-discovery\t0",
        "SERVER\t192.168.1.66\tbad;host\tBAD\t\tgvfs\t0",
        "SERVER\ttoo-few-fields",
        "STATUS\tgvfs\tdns-sd://local/: simulated failure",
        "SERVER\t192.168.1.104\t192.168.1.104\tDELL-740\tWorkgroup:MAKHLOUF\tws-discovery\t0",
    ]
    for record in records:
        print(record, flush=True)
        time.sleep(0.1)
    print("DONE", flush=True)
    sys.exit(0)
if sys.argv[1] == "--shares":
    assert sys.argv[2].startswith("smb://")
    if sys.argv[2] in ("smb://auth.test", "smb://cancel.test"):
        print("AUTH", flush=True)
        credentials = sys.stdin.readline()
        if not credentials:
            sys.exit(1)
        assert credentials == "dGVzdC11c2Vy\tV09SS0dST1VQ\t\n"
    time.sleep(0.15)
    entry("Team files", mode=stat.S_IFDIR | 0o555)
    entry("5TB", mode=stat.S_IFDIR | 0o555)
    print("DONE", flush=True)
    sys.exit(0)

path = sys.argv[2].rstrip("/")
if path.endswith("/failure"):
    print("ERROR simulated remote permission failure", flush=True)
    sys.exit(1)
if path.endswith("/invalid"):
    print("not a directory entry", flush=True)
    sys.exit(0)
time.sleep(0.15)
if path.endswith("/large"):
    for start in range(0, 793, 100):
        for index in range(start, min(start + 100, 793)):
            entry(f"folder-{index}")
        sys.stdout.flush()
        time.sleep(0.12)
else:
    entry("subfolder")
    entry("link-folder", target="subfolder", symlink=True)
    entry("broken-link", mode=stat.S_IFLNK | 0o777, target="missing", symlink=True)
print("DONE", flush=True)
if path.endswith("/eof-before-exit"):
    sys.stdout.close()
    time.sleep(0.4)
