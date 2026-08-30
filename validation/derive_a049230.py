#!/usr/bin/env python3
"""Fail-closed derivation and provenance checks for OEIS A049230."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


CRIPPEN_TOTALS = {
    5: 8,
    6: 52,
    7: 270,
    8: 1446,
    9: 7578,
    10: 38473,
    11: 191154,
}

# Exact published, noncontiguous checkpoints. They are validation evidence,
# never permission to emit a contiguous OEIS extension across n=19..22.
PUBLISHED_CHECKPOINTS = {
    23: (856_234_452_257_592, 17_838_020_877_573),
    24: (3_793_782_481_788_456, 79_036_635_370_667),
    25: (16_740_109_801_232_352, 348_751_024_443_185),
    26: (73_645_193_456_914_224, 1_534_271_679_749_333),
    27: (322_845_646_222_032_480, 6_725_942_965_307_646),
}


def read_rows(path: Path, *, terminal_blank: bool) -> dict[int, int]:
    payload = path.read_bytes()
    if terminal_blank:
        if not payload.endswith(b"\n\n") or payload.endswith(b"\n\n\n"):
            raise ValueError(f"{path}: expected exactly one terminal empty record")
        payload = payload[:-1]
    elif not payload.endswith(b"\n") or payload.endswith(b"\n\n"):
        raise ValueError(f"{path}: expected one final newline and no blank record")
    if b"\0" in payload or b"\r" in payload:
        raise ValueError(f"{path}: forbidden NUL or CR byte")
    try:
        text = payload.decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError(f"{path}: non-ASCII content") from error

    rows: dict[int, int] = {}
    for line_number, line in enumerate(text.splitlines(), start=1):
        fields = line.split(" ")
        if len(fields) != 2 or not all(field.isdecimal() for field in fields):
            raise ValueError(f"{path}:{line_number}: malformed row")
        index, value = map(int, fields)
        if index < 1 or value < 0 or index in rows:
            raise ValueError(f"{path}:{line_number}: invalid or duplicate index")
        if line != f"{index} {value}":
            raise ValueError(f"{path}:{line_number}: noncanonical formatting")
        rows[index] = value
    if not rows or sorted(rows) != list(range(1, max(rows) + 1)):
        raise ValueError(f"{path}: rows must be exactly contiguous from index 1")
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--p3", type=Path, required=True)
    parser.add_argument("--a033323", type=Path, required=True)
    parser.add_argument("--a049230-prefix", type=Path, required=True)
    parser.add_argument("--expect-p3", type=Path)
    arguments = parser.parse_args()

    p3 = read_rows(arguments.p3, terminal_blank=False)
    planar = read_rows(arguments.a033323, terminal_blank=True)
    prefix = read_rows(arguments.a049230_prefix, terminal_blank=True)
    if max(prefix) != 18:
        raise ValueError("A049230 prefix must contain exactly n=1..18")
    if max(planar) < max(max(p3), max(PUBLISHED_CHECKPOINTS)):
        raise ValueError("A033323 input does not cover all required indices")

    if arguments.expect_p3 is not None:
        expected = read_rows(arguments.expect_p3, terminal_blank=False)
        for n in range(1, min(max(p3), max(expected)) + 1):
            if p3[n] != expected[n]:
                raise ValueError(
                    f"p3 replay mismatch at n={n}: {p3[n]} != {expected[n]}"
                )

    reconstructed: dict[int, int] = {}
    for n, value in p3.items():
        reconstructed[n] = 3 * planar[n] + 48 * value
        if n in prefix and reconstructed[n] != prefix[n]:
            raise ValueError(
                f"A049230 prefix mismatch at n={n}: "
                f"{reconstructed[n]} != {prefix[n]}"
            )

    for n, expected_total in CRIPPEN_TOTALS.items():
        if n <= max(p3):
            numerator = reconstructed[n] + 3 * planar[n]
            value, remainder = divmod(numerator, 48)
            if remainder or value != expected_total:
                raise ValueError(f"Crippen normalization mismatch at n={n}")

    for n, (a049230, expected_p3) in PUBLISHED_CHECKPOINTS.items():
        numerator = a049230 - 3 * planar[n]
        value, remainder = divmod(numerator, 48)
        if numerator < 0 or remainder or value != expected_p3:
            raise ValueError(f"published checkpoint normalization failure at n={n}")
        if n in p3 and p3[n] != expected_p3:
            raise ValueError(f"computed p3 disagrees with checkpoint at n={n}")

    for n in sorted(reconstructed):
        print(n, reconstructed[n])
    print(
        f"DERIVATION PASS: p3 n=1..{max(p3)}; "
        "A049230 prefix n=1..18; Crippen n=5..11; "
        "published checkpoints n=23..27",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"DERIVATION FAIL: {error}", file=sys.stderr)
        raise SystemExit(1) from error
