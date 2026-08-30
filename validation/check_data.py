#!/usr/bin/env python3
"""Fail-closed consistency checks for the public A049230 package."""

from __future__ import annotations

import hashlib
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parent.parent


def read_plain(path: Path, *, terminal_blank: bool) -> dict[int, int]:
    raw = path.read_bytes()
    if terminal_blank:
        if not raw.endswith(b"\n\n") or raw.endswith(b"\n\n\n"):
            raise ValueError(f"{path}: expected final bytes 0a 0a")
        raw = raw[:-1]
    elif not raw.endswith(b"\n") or raw.endswith(b"\n\n"):
        raise ValueError(f"{path}: expected one final LF")
    if b"\r" in raw or b"\0" in raw:
        raise ValueError(f"{path}: forbidden CR or NUL")
    rows: dict[int, int] = {}
    for number, line in enumerate(raw.decode("ascii").splitlines(), start=1):
        fields = line.split(" ")
        if len(fields) != 2 or not all(field.isdecimal() for field in fields):
            raise ValueError(f"{path}:{number}: malformed row")
        n, value = map(int, fields)
        if n in rows or line != f"{n} {value}":
            raise ValueError(f"{path}:{number}: duplicate or noncanonical row")
        rows[n] = value
    if sorted(rows) != list(range(1, max(rows) + 1)):
        raise ValueError(f"{path}: noncontiguous indices")
    return rows


def read_result(path: Path, header: str) -> dict[int, int]:
    raw = path.read_bytes()
    if not raw.endswith(b"\n") or raw.endswith(b"\n\n"):
        raise ValueError(f"{path}: expected one final LF")
    if b"\r" in raw or b"\0" in raw:
        raise ValueError(f"{path}: forbidden CR or NUL")
    lines = raw.decode("ascii").splitlines()
    if not lines or lines[0] != header:
        raise ValueError(f"{path}: unexpected header")
    rows: dict[int, int] = {}
    for number, line in enumerate(lines[1:], start=2):
        fields = line.split("\t")
        if len(fields) != 2 or not all(field.isdecimal() for field in fields):
            raise ValueError(f"{path}:{number}: malformed row")
        n, value = map(int, fields)
        if n in rows:
            raise ValueError(f"{path}:{number}: duplicate index")
        rows[n] = value
    if sorted(rows) != list(range(1, max(rows) + 1)):
        raise ValueError(f"{path}: noncontiguous indices")
    return rows


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    data = ROOT / "data"
    results = ROOT / "results"
    validation = ROOT / "validation"

    a049230 = read_plain(data / "b049230.txt", terminal_blank=True)
    a033323 = read_plain(data / "b033323.txt", terminal_blank=True)
    prefix = read_plain(validation / "prefix18.txt", terminal_blank=True)
    p3_prefix = read_plain(validation / "p3_prefix.tsv", terminal_blank=False)
    p3_run = read_plain(validation / "run" / "results.tsv", terminal_blank=False)
    public_a = read_result(results / "a049230.tsv", "# n\ta_n")
    public_p3 = read_result(results / "p3_orbits.tsv", "# n\tp3_orbits")

    if max(a049230) != 27 or len(a049230) != 27:
        raise ValueError("b049230.txt must cover exactly n=1..27")
    if max(a033323) < 27:
        raise ValueError("b033323.txt must cover n=1..27")
    if prefix != {n: a049230[n] for n in range(1, 19)}:
        raise ValueError("historical prefix is not b049230.txt n=1..18")
    if p3_prefix != {n: p3_run[n] for n in range(1, 19)}:
        raise ValueError("p3 prefix does not match the campaign result")
    if public_p3 != p3_run:
        raise ValueError("public p3 result does not match the campaign state")
    if public_a != {n: a049230[n] for n in range(1, 23)}:
        raise ValueError("public campaign-derived totals do not match b049230.txt")

    terms_path = data / "certified_terms.tsv"
    raw_terms = terms_path.read_bytes()
    if not raw_terms.endswith(b"\n\n") or raw_terms.endswith(b"\n\n\n"):
        raise ValueError("certified_terms.tsv must end with final bytes 0a 0a")
    lines = raw_terms.decode("ascii").splitlines()
    if lines[0] != "# n\ta_n\tA033323_n\tp3_orbits\tprovenance":
        raise ValueError("certified_terms.tsv: unexpected header")
    allowed = {
        **{n: "prior_oeis" for n in range(1, 19)},
        **{n: "campaign" for n in range(19, 23)},
        23: "published_lee",
        **{n: "published_hsieh" for n in range(24, 28)},
    }
    certified: dict[int, tuple[int, int, int, str]] = {}
    for number, line in enumerate(lines[1:], start=2):
        if not line:
            continue
        fields = line.split("\t")
        if len(fields) != 5 or not all(field.isdecimal() for field in fields[:4]):
            raise ValueError(f"certified_terms.tsv:{number}: malformed row")
        n, value, planar, p3 = map(int, fields[:4])
        source = fields[4]
        if n in certified or source != allowed.get(n):
            raise ValueError(f"certified_terms.tsv:{number}: invalid index/source")
        if value != 3 * planar + 48 * p3:
            raise ValueError(f"certified_terms.tsv:{number}: reconstruction failure")
        if value != a049230[n] or planar != a033323[n]:
            raise ValueError(f"certified_terms.tsv:{number}: source mismatch")
        certified[n] = (value, planar, p3, source)
    if sorted(certified) != list(range(1, 28)):
        raise ValueError("certified_terms.tsv must cover exactly n=1..27")

    manifest = (validation / "run" / "manifest.tsv").read_bytes().splitlines()
    if len(manifest) < 4 or manifest[0] != b"A049230-MANIFEST":
        raise ValueError("campaign manifest magic is missing")
    config = manifest[1].decode("ascii").split("\t")
    state = manifest[2].decode("ascii").split("\t")
    if config[:2] != ["CONFIG", "A049230"] or len(config) != 11:
        raise ValueError("unexpected campaign CONFIG row")
    if config[2] != sha256(ROOT / "src" / "a049230.c"):
        raise ValueError("manifest/source identity mismatch")
    if config[3] != sha256(ROOT / "src" / "Makefile"):
        raise ValueError("manifest/Makefile identity mismatch")
    if config[4:] != ["22", "10", "8", "64", "154271", "2411", "4500"]:
        raise ValueError("unexpected frozen campaign configuration")
    if state[:4] != ["STATE", "2417", "3", "0"] or state[-1] != "2411":
        raise ValueError("campaign state is not complete")
    unit_rows = [line for line in manifest if line.startswith(b"UNIT\t")]
    if len(unit_rows) != 2411:
        raise ValueError("campaign manifest must contain 2411 UNIT rows")

    print("DATA PASS: b-files, results, certified terms, and manifest agree")
    print("ENDPOINT PASS: contiguous A049230 coverage is n=1..27")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, UnicodeError, ValueError) as error:
        print(f"DATA FAIL: {error}", file=sys.stderr)
        raise SystemExit(1) from error
