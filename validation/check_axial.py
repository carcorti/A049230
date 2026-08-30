#!/usr/bin/env python3
"""Strict aggregation of the published Shirai--Sakumichi m=2 axial data."""

from __future__ import annotations

import argparse
import csv
import hashlib
from pathlib import Path
import sys


EXPECTED = {19: 6_927_757_800, 20: 28_720_091_852}
EXPECTED_SHA256 = "5924996f048830b76a81854c33cbbcbc6a29b13600b3ea7e4e59db0fe6908e78"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    arguments = parser.parse_args()

    payload = arguments.input.read_bytes()
    actual_sha256 = hashlib.sha256(payload).hexdigest()
    if actual_sha256 != EXPECTED_SHA256:
        raise ValueError(
            f"input SHA-256 {actual_sha256} != pinned {EXPECTED_SHA256}"
        )
    if b"\0" in payload or b"\r" in payload or not payload.endswith(b"\n"):
        raise ValueError("input must be NUL-free LF-terminated text")
    rows = payload.decode("ascii").splitlines()
    reader = csv.DictReader(rows)
    if reader.fieldnames != ["n", "r", "m", "b", "W"]:
        raise ValueError("unexpected CSV header")

    totals = {19: 0, 20: 0}
    seen: set[tuple[int, int, int, int]] = set()
    for line_number, row in enumerate(reader, start=2):
        if None in row or any(value is None or not value.isdecimal() for value in row.values()):
            raise ValueError(f"line {line_number}: malformed nonnegative integer row")
        n, r, m, b, value = (int(row[key]) for key in reader.fieldnames)
        key = (n, r, m, b)
        if key in seen:
            raise ValueError(f"line {line_number}: duplicate key {key}")
        seen.add(key)
        if n in totals and m == 2:
            totals[n] += value

    if totals != EXPECTED:
        raise ValueError(f"m=2 axial totals {totals} != {EXPECTED}")
    for n in sorted(totals):
        print(n, totals[n], 6 * totals[n])
    print("AXIAL DATA PASS: exact m=2 subsets at n=19,20", file=sys.stderr)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, UnicodeError, ValueError) as error:
        print(f"AXIAL DATA FAIL: {error}", file=sys.stderr)
        raise SystemExit(1) from error
