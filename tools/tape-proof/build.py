#!/usr/bin/env python3
"""Builds proof.html: inlines check.js, and optionally the published key fingerprints and an example bundle."""
import argparse
import json
import pathlib
import re
import sys

here = pathlib.Path(__file__).resolve().parent


def embed(value):
    return json.dumps(value, separators=(",", ":")).replace("</", "<\\/")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--fingerprints", help="file with one 'voter N  HEX' line per voter, as the key ceremony prints")
    ap.add_argument("--sample", help="a proof bundle to show when the page opens")
    a = ap.parse_args()
    pinned = None
    if a.fingerprints:
        pinned = {}
        for line in pathlib.Path(a.fingerprints).read_text().splitlines():
            m = re.fullmatch(r"\s*voter\s+([123])\s+([0-9a-f]{64})\s*", line)
            if m:
                pinned[m.group(1)] = m.group(2)
        if sorted(pinned) != ["1", "2", "3"]:
            sys.exit("fingerprints file must name voters 1, 2 and 3")
    if a.sample and not pathlib.Path(a.sample).is_file():
        sys.exit(f"{a.sample} does not exist; write one first with velocita-tape-verify --book SYMBOL --until-seq N --bundle FILE")
    sample = json.loads(pathlib.Path(a.sample).read_text()) if a.sample else None
    page = (here / "page.html").read_text()
    script = (here / "check.js").read_text()
    if "</script" in script:
        sys.exit("check.js cannot contain a closing script tag")
    for marker in ("/*CHECK_JS*/", "/*PINNED*/null", "/*SAMPLE*/null"):
        if page.count(marker) != 1:
            sys.exit("page.html must contain " + marker + " once")
    page = page.replace("/*CHECK_JS*/", script).replace("/*PINNED*/null", embed(pinned)).replace("/*SAMPLE*/null", embed(sample))
    pathlib.Path(a.out).write_text(page)
    print(a.out)


if __name__ == "__main__":
    main()
