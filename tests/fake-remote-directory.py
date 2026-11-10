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
    assert sys.argv[2:] in (["-b", "/dev/test-usb1"], ["-b", "/dev/test-usb"],
                            ["-b", "/dev/test-usb2"])
    with open(os.environ["SWORDFISH_TEST_MOUNT_LOG"], "a") as log:
        log.write("disk " + action + "\n")
    if action == "unmount" and os.environ.get("SWORDFISH_TEST_DISK_BUSY"):
        print("org.freedesktop.UDisks2.Error.DeviceBusy: target is busy")
        sys.exit(1)
    if action == "unmount" and os.environ.get("SWORDFISH_TEST_DISK_STILL_MOUNTED"):
        sys.exit(0)
    busy_once = os.environ.get("SWORDFISH_TEST_DISK_BUSY_ONCE")
    if action == "unmount" and busy_once and os.path.exists(busy_once):
        os.unlink(busy_once)
        print("org.freedesktop.UDisks2.Error.DeviceBusy: target is busy")
        sys.exit(1)
    if action in ("mount", "unmount"):
        previous = []
        if os.path.exists(os.environ["SWORDFISH_TEST_DISK_MTAB"]):
            with open(os.environ["SWORDFISH_TEST_DISK_MTAB"]) as table:
                previous = [line for line in table if line.startswith("/dev/test-usb") and
                            not line.startswith(sys.argv[-1] + " ")]
        with open(os.environ["SWORDFISH_TEST_DISK_MTAB"], "w") as table:
            table.write("/dev/root / ext4 rw 0 0\n")
            table.writelines(previous)
            if action == "mount":
                table.write("/dev/test-usb1 " + os.environ["SWORDFISH_TEST_DISK_PATH"] + " ext4 rw 0 0\n")
    else:
        assert action == "power-off"
        if os.environ.get("SWORDFISH_TEST_POWER_OFF_FAIL"):
            print("Power-off failed")
            sys.exit(1)
        volumes = os.environ.get("SWORDFISH_TEST_EJECT_VOLUMES")
        if volumes:
            with open(volumes) as inventory:
                remaining = [line for line in inventory if not line.startswith('PATH="/dev/test-usb')]
            with open(volumes, "w") as inventory:
                inventory.writelines(remaining)
    sys.exit(0)

if os.path.basename(sys.argv[0]) == "lsblk":
    assert not ("-l" in sys.argv and "-P" in sys.argv), "lsblk --list and --pairs are mutually exclusive"
    assert not os.environ.get("LD_LIBRARY_PATH")
    assert not os.environ.get("GIO_MODULE_DIR")
    if "HOTPLUG" in sys.argv:
        print("1" if sys.argv[-1] == "/dev/test-usb1" else "0")
    elif "NAME,TYPE" in sys.argv:
        print("/dev/test-usb1 part\n/dev/test-usb disk")
    elif "NAME" in sys.argv:
        print("/dev/test-usb\n/dev/test-usb1\n/dev/test-usb2")
    elif os.environ.get("SWORDFISH_TEST_VOLUMES"):
        with open(os.environ["SWORDFISH_TEST_VOLUMES"]) as volumes:
            print(volumes.read(), end="")
    sys.exit(0)

if os.path.basename(sys.argv[0]) == "fake-player":
    assert not os.environ.get("LD_LIBRARY_PATH")
    with open(os.environ["SWORDFISH_TEST_MOUNT_LOG"], "a") as log:
        log.write("player " + " ".join(sys.argv[1:]) + "\n")
    sys.exit(0)

if os.path.basename(sys.argv[0]) == "xdg-open":
    assert not os.environ.get("LD_LIBRARY_PATH")
    assert not os.environ.get("GIO_MODULE_DIR")
    assert len(sys.argv) == 2 and sys.argv[1] in (
        "https://github.com/hasmak-44/Swordfish-FM", "http://192.168.1.50/",
        "http://192.168.1.70:8200/MediaItems/1.mp3"), sys.argv
    with open(os.environ["SWORDFISH_TEST_MOUNT_LOG"], "a") as log:
        log.write("website " + sys.argv[1] + "\n")
    sys.exit(0)

if os.path.basename(sys.argv[0]) == "gio":
    assert not os.environ.get("LD_LIBRARY_PATH")
    assert not os.environ.get("GIO_MODULE_DIR")
    state = os.environ["SWORDFISH_TEST_MOUNT_LOG"]
    gvfs = os.environ.get("SWORDFISH_TEST_GVFS_ROOT")
    remount = os.environ.get("SWORDFISH_TEST_REMOUNT_DIR")
    if remount and sys.argv[1] == "info":
        if os.path.isdir(remount):
            print("local path: " + os.environ["SWORDFISH_TEST_MOUNT_PATH"])
            sys.exit(0)
        print("gio: location is not mounted")
        sys.exit(1)
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
        if os.environ.get("SWORDFISH_TEST_GVFS_UNMOUNT_DIR"):
            os.rmdir(os.environ["SWORDFISH_TEST_GVFS_UNMOUNT_DIR"])
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
        if remount:
            os.mkdir(remount)
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
    if os.environ.get("SWORDFISH_TEST_SERVICES"):
        records += [
            "SERVICE\tprinter\tprinter@192.168.1.50\t192.168.1.50\tOffice Printer\thttp://192.168.1.50/\tmdns",
            "SERVICE\tmedia\tmedia@http://192.168.1.70:8200/rootDesc.xml\t192.168.1.70\tLiving Room"
            "\thttp://192.168.1.70:8200/rootDesc.xml\tssdp",
            "SERVICE\tftp\tftp@192.168.1.60:21\t192.168.1.60\tNAS\tftp://192.168.1.60:21/\tmdns",
            "SERVICE\trouter\trouter@192.168.1.1\t192.168.1.1\tHome Router\thttp://192.168.1.1/\tssdp",
            "SERVICE\tsftp\tsftp@192.168.1.60:22\t192.168.1.60\tNAS\tsftp://192.168.1.60:22/\tmdns",
            # Rejected: unknown kind, unsafe address, script URL, wrong field count
            "SERVICE\ttoaster\ttoaster@192.168.1.9\t192.168.1.9\tToaster\thttp://192.168.1.9/\tssdp",
            "SERVICE\tprinter\tprinter@x\tbad;host\tBad\thttp://192.168.1.9/\tmdns",
            "SERVICE\tprinter\tprinter@192.168.1.8\t192.168.1.8\tEvil\tjavascript:alert(1)\tmdns",
            "SERVICE\tftp\ttoo-few-fields",
            # A later record for the same device replaces the earlier one
            "SERVICE\tprinter\tprinter@192.168.1.50\t192.168.1.50\tOffice Laser\thttp://192.168.1.50/\tmdns,ssdp",
        ]
    for record in records:
        print(record, flush=True)
        time.sleep(0.1)
    print("DONE", flush=True)
    sys.exit(0)
if sys.argv[1] == "--media":
    assert sys.argv[2] == "http://192.168.1.70:8200/rootDesc.xml", sys.argv
    path = sys.argv[3].rstrip("/")
    time.sleep(0.15)
    if path == "dlna://192.168.1.70:8200":
        entry("Music", mode=stat.S_IFDIR | 0o555)
        entry("Video", mode=stat.S_IFDIR | 0o555)
    elif path == "dlna://192.168.1.70:8200/Music":
        entry("Song One.mp3", mode=stat.S_IFREG | 0o444, target="http://192.168.1.70:8200/MediaItems/1.mp3")
        entry("Live 50%.flac", mode=stat.S_IFREG | 0o444, target="http://192.168.1.70:8200/MediaItems/2.flac")
        entry("Albums", mode=stat.S_IFDIR | 0o555)
    elif path == "dlna://192.168.1.70:8200/Music/Albums":
        pass
    else:
        print("ERROR This folder is no longer on the media server.", flush=True)
        sys.exit(1)
    print("DONE", flush=True)
    sys.exit(0)
if sys.argv[1] == "--download":
    url, destination = sys.argv[2], sys.argv[3]
    with open(os.environ["SWORDFISH_TEST_MOUNT_LOG"], "a") as log:
        log.write("download " + url + "\n")
    if url.endswith("/missing.mp3"):
        print("ERROR The media server answered 404 Not Found", flush=True)
        sys.exit(1)
    if url.endswith("/slow.mp3"):
        # Stays in progress until the test cancels it
        fd = os.open(destination + ".part", os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
        os.write(fd, b"x" * 512)
        print("PROGRESS 0 2048\nPROGRESS 512 2048", flush=True)
        time.sleep(60)
        sys.exit(1)
    print("PROGRESS 0 -1", flush=True)
    fd = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
    os.write(fd, ("media:" + url).encode())
    os.close(fd)
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
