#!/usr/bin/env python3
"""Render the approved category icons; rsvg-convert is a maintainer tool only."""

from pathlib import Path
import subprocess

for source in sorted(Path(__file__).parent.glob("pref-*.svg")):
    drawing = source.read_bytes()
    for selected in (False, True):
        output = source.with_name(source.stem + ("-selected" if selected else "") + ".png")
        subprocess.run(
            ["rsvg-convert", "-w", "96", "-h", "96", "-o", str(output)],
            input=drawing.replace(b"#0F56D9", b"#FFFFFF") if selected else drawing,
            check=True,
        )
