#!/usr/bin/env python3
import ast
import base64
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile


with tempfile.TemporaryDirectory(prefix="swordfish-native-helper-") as temporary:
    root = Path(temporary)
    for index in range(793):
        (root / f"folder-{index}").mkdir()
    unusual = "space quote' and\nnewline"
    (root / unusual).write_text("hello")
    (root / "link-folder").symlink_to("folder-0")
    (root / "broken-link").symlink_to("missing")
    result = subprocess.run([sys.argv[1], "--list", str(root)], capture_output=True, timeout=10)
    assert result.returncode == 0, result.stderr
    assert not result.stderr, result.stderr
    records = result.stdout.decode().splitlines()
    assert records.pop() == "DONE"
    rows = {}
    for record in records:
        record = re.sub(r"\b(?:uint32|int64) ", "", record)
        record = record.replace(" true,", " True,").replace(" false,", " False,")
        value = ast.literal_eval(record)
        assert len(value) == 11
        name = base64.b64decode(value[0], validate=True).decode()
        assert name not in rows
        rows[name] = value
    assert len(rows) == 796
    assert stat.S_ISDIR(rows["folder-792"][1])
    assert stat.S_ISDIR(rows["link-folder"][1]) and rows["link-folder"][8]
    assert stat.S_ISLNK(rows["broken-link"][1]) and rows["broken-link"][8]
    assert rows[unusual][6] == 5
    assert base64.b64decode(rows["broken-link"][7]) == b"missing"
    failure = subprocess.run(
        [sys.argv[1], "--list", str(root / "missing")], capture_output=True, timeout=10
    )
    assert failure.returncode != 0 and failure.stdout.startswith(b"ERROR ")
    assert b"DONE" not in failure.stdout
    print("PASS: native async GIO helper, 796 entries, special names, symlinks, and explicit errors")
