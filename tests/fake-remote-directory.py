#!/usr/bin/env python3
import base64
import os
import stat
import sys
import time


def entry(name, mode=stat.S_IFDIR | 0o755, target="", symlink=False):
    encoded = base64.b64encode(name.encode()).decode()
    link = base64.b64encode(target.encode()).decode()
    print(
        f"('{encoded}', uint32 {mode}, uint32 {os.getuid()}, uint32 {os.getgid()}, "
        f"int64 1, int64 1, int64 0, '{link}', {'true' if symlink else 'false'}, 1, 1)"
    )


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
