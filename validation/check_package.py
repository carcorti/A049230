#!/usr/bin/env python3
"""Fail-closed publication-boundary and inventory audit for this package."""

from __future__ import annotations

import hashlib
from pathlib import Path
import re
import struct
import sys


ROOT = Path(__file__).resolve().parent.parent
CHECKSUMS = ROOT / "validation" / "checksums.sha256"
BINARY_FILES = {"figures/A049230_infographic.png"}
EXPECTED_FILES = {
    ".gitignore",
    "CITATION.cff",
    "LICENSE",
    "NOTICE.md",
    "README.md",
    "data/b033323.txt",
    "data/b049230.txt",
    "data/certified_terms.tsv",
    "figures/A049230_infographic.png",
    "paper/A049230_v5.tex",
    "results/a049230.tsv",
    "results/p3_orbits.tsv",
    "src/Makefile",
    "src/a049230.c",
    "validation/axial_data.txt",
    "validation/check_axial.py",
    "validation/check_blank.py",
    "validation/check_data.py",
    "validation/check_package.py",
    "validation/checksums.sha256",
    "validation/completion.md",
    "validation/derive_a049230.py",
    "validation/direct_oracle.py",
    "validation/joeis/A049230Verifier.java",
    "validation/joeis/Animal.java",
    "validation/joeis/joeis_notes.md",
    "validation/joeis/run_verifier.sh",
    "validation/orbit_oracle.py",
    "validation/p3_prefix.tsv",
    "validation/prefix18.txt",
    "validation/run.sh",
    "validation/run/manifest.tsv",
    "validation/run/results.tsv",
    "validation/validation_notes.md",
    "validation/validation_summary.md",
}
EXPECTED_DIRECTORIES = {
    "data",
    "figures",
    "paper",
    "results",
    "src",
    "validation",
    "validation/joeis",
    "validation/run",
}


def fail(message: str) -> None:
    raise ValueError(message)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_checksums() -> dict[str, str]:
    rows: dict[str, str] = {}
    for number, line in enumerate(CHECKSUMS.read_text(encoding="ascii").splitlines(), 1):
        match = re.fullmatch(r"([0-9a-f]{64})  \./(.+)", line)
        if match is None:
            fail(f"checksums.sha256:{number}: malformed row")
        value, name = match.groups()
        if name in rows or name == "validation/checksums.sha256":
            fail(f"checksums.sha256:{number}: duplicate or self entry")
        rows[name] = value
    return rows


def main() -> int:
    files = {
        path.relative_to(ROOT).as_posix()
        for path in ROOT.rglob("*")
        if path.is_file()
    }
    directories = {
        path.relative_to(ROOT).as_posix()
        for path in ROOT.rglob("*")
        if path.is_dir()
    }
    links = [path for path in ROOT.rglob("*") if path.is_symlink()]
    if links:
        fail("symbolic links are not allowed in the raw package")
    if files != EXPECTED_FILES:
        missing = sorted(EXPECTED_FILES - files)
        extra = sorted(files - EXPECTED_FILES)
        fail(f"public inventory mismatch; missing={missing}; extra={extra}")
    if directories != EXPECTED_DIRECTORIES:
        missing = sorted(EXPECTED_DIRECTORIES - directories)
        extra = sorted(directories - EXPECTED_DIRECTORIES)
        fail(f"public directory mismatch; missing={missing}; extra={extra}")
    if sum(name.endswith("README.md") for name in files) != 1:
        fail("the package must contain exactly one README.md")
    if any(len(Path(name).name) > 25 for name in files):
        fail("a public basename exceeds 25 characters")
    if any(Path(name).suffix.lower() == ".pdf" for name in files):
        fail("PDF files are excluded from the raw package")

    decoded: dict[str, str] = {}
    for name in sorted(files):
        payload = (ROOT / name).read_bytes()
        if name in BINARY_FILES:
            if not payload.startswith(b"\x89PNG\r\n\x1a\n") or len(payload) < 24:
                fail(f"{name}: invalid PNG signature or truncated header")
            width, height = struct.unpack(">II", payload[16:24])
            if (width, height) != (1600, 900):
                fail(f"{name}: expected 1600x900 PNG, found {width}x{height}")
            continue
        if not payload or b"\0" in payload or b"\r" in payload:
            fail(f"{name}: empty file, NUL byte, or CR byte")
        if not payload.endswith(b"\n"):
            fail(f"{name}: missing final LF")
        try:
            decoded[name] = payload.decode("utf-8")
        except UnicodeDecodeError as error:
            raise ValueError(f"{name}: non-UTF-8 content") from error

    public_text = "\n".join(
        text for name, text in decoded.items()
        if name != "validation/check_package.py"
    )
    if "/home/" in public_text:
        fail("a workstation-local absolute path is present")
    if re.search(r"A049230_v[1234](?:\.|\b)", public_text):
        fail("a superseded manuscript basename is referenced")
    if re.search(r"\bSofia\b", public_text, re.IGNORECASE):
        fail("the excluded reviewer alias is present")
    if sorted(path.name for path in (ROOT / "paper").iterdir()) != ["A049230_v5.tex"]:
        fail("the provisional paper inventory is not exactly A049230_v5.tex")
    readme = decoded["README.md"]
    for name in sorted(EXPECTED_FILES - {"README.md"}):
        if name not in readme:
            fail(f"README.md inventory omits {name}")

    cff = decoded["CITATION.cff"]
    if 'repository-code: "https://github.com/carcorti/A049230"' not in cff:
        fail("CITATION.cff repository-code mismatch")
    if 'version: "v1.0"' not in cff:
        fail("CITATION.cff provisional version mismatch")
    if cff.count("10.5281/zenodo.xxxxxxxx") != 4:
        fail("CITATION.cff must contain all four DOI placeholders")
    if decoded["paper/A049230_v5.tex"].count("10.5281/zenodo.xxxxxxxx") != 1:
        fail("the provisional manuscript DOI placeholder changed")
    notice = decoded["NOTICE.md"]
    if "CC BY 4.0" not in notice or "not relicensed under MIT" not in notice:
        fail("third-party axial-data licensing is not explicit")

    for name, expected_header, columns in (
        ("data/certified_terms.tsv", "# n\ta_n\tA033323_n\tp3_orbits\tprovenance", 5),
        ("results/a049230.tsv", "# n\ta_n", 2),
        ("results/p3_orbits.tsv", "# n\tp3_orbits", 2),
    ):
        lines = decoded[name].splitlines()
        if lines[0] != expected_header:
            fail(f"{name}: TSV header mismatch")
        if any(len(line.split("\t")) != columns for line in lines[1:] if line):
            fail(f"{name}: non-tabular data row")

    for name in ("validation/p3_prefix.tsv", "validation/run/results.tsv"):
        lines = decoded[name].splitlines()
        if any(not re.fullmatch(r"[0-9]+ [0-9]+", line) for line in lines):
            fail(f"{name}: frozen raw space-separated grammar changed")

    checksums = read_checksums()
    expected_checksum_names = EXPECTED_FILES - {"validation/checksums.sha256"}
    if set(checksums) != expected_checksum_names:
        fail("checksum inventory does not match the public file inventory")
    for name, expected in checksums.items():
        if digest(ROOT / name) != expected:
            fail(f"checksum mismatch: {name}")

    print(f"PACKAGE PASS: {len(files)} files; inventory, policy, formats, and checksums agree")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, UnicodeError, ValueError) as error:
        print(f"PACKAGE FAIL: {error}", file=sys.stderr)
        raise SystemExit(1) from error
