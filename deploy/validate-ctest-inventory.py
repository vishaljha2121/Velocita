#!/usr/bin/env python3
import argparse
import json
import pathlib
import sys


parser = argparse.ArgumentParser()
parser.add_argument("--inventory", required=True, type=pathlib.Path)
parser.add_argument("--expected", required=True, nargs="+")
args = parser.parse_args()

try:
    document = json.loads(args.inventory.read_text(encoding="utf-8"))
    tests = document["tests"]
    actual = [test["name"] for test in tests]
except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
    print(f"invalid CTest JSON inventory: {error}", file=sys.stderr)
    raise SystemExit(1)

if len(actual) != len(set(actual)):
    print("CTest inventory contains duplicate suite names", file=sys.stderr)
    raise SystemExit(1)

for test in tests:
    for prop in test.get("properties", []):
        name = prop.get("name")
        value = prop.get("value")
        prohibited = (
            (name == "DISABLED" and bool(value))
            or name in {"SKIP_RETURN_CODE", "SKIP_REGULAR_EXPRESSION"}
        )
        if prohibited:
            print(
                f"required suite is not unconditionally runnable: "
                f"{test['name']} ({name})",
                file=sys.stderr,
            )
            raise SystemExit(1)

expected = set(args.expected)
actual_set = set(actual)
missing = sorted(expected - actual_set)
unexpected = sorted(actual_set - expected)
if missing:
    print(f"missing required CTest suites: {', '.join(missing)}", file=sys.stderr)
if unexpected:
    print(f"unexpected CTest suites: {', '.join(unexpected)}", file=sys.stderr)
if missing or unexpected:
    raise SystemExit(1)
