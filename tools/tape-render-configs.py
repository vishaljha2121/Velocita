#!/usr/bin/env python3
"""Renders the tape's per-host configs from deploy/tape/templates with one shared incarnation."""
import argparse
import secrets
import sys
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument("out", type=Path, help="new directory; gets host-1/, host-2/, host-3/")
    p.add_argument("--incarnation", type=int, default=0, help="reuse an incarnation (0 makes a new one)")
    a = p.parse_args()
    if a.out.exists():
        sys.exit(f"tape-render-configs: {a.out} already exists")
    incarnation = a.incarnation or secrets.randbits(48) or 1
    templates = Path(__file__).resolve().parent.parent / "deploy/tape/templates"
    for n in (1, 2, 3):
        host = a.out / f"host-{n}"
        host.mkdir(parents=True)
        for kind in ("voter", "recorder"):
            text = (templates / f"{kind}-{n}.conf.template").read_text().replace("@INCARNATION@", str(incarnation))
            if "@" in text:
                sys.exit(f"tape-render-configs: unfilled placeholder in {kind}-{n}")
            (host / f"{kind}.conf").write_text(text)
    (a.out / "INCARNATION").write_text(f"{incarnation}\n")
    print(f"tape-render-configs: incarnation {incarnation} -> {a.out}")


if __name__ == "__main__":
    main()
