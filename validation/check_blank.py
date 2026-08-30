#!/usr/bin/env python3
"""Fail unless every named OEIS text artifact ends in exactly one blank line."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args()

    failed = False
    for path in args.paths:
        raw = path.read_bytes()
        valid = raw.endswith(b"\n\n") and not raw.endswith(b"\n\n\n")
        print(f"{path}: {'PASS' if valid else 'FAIL'} (required terminal bytes: 0a 0a)")
        failed |= not valid
    if failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
