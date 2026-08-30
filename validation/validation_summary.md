# Validation summary

**Date:** 30 August 2026  
**Scope:** final GitHub release candidate `v1.0.1` for OEIS A049230  
**Verdict:** complete release-package validation PASS

## Executed public-package checks

| Check | Public command | Result |
| --- | --- | --- |
| Data, provenance, manifest identity, and endpoint | `python3 validation/check_data.py` | PASS |
| Public-tree inventory, policy, formats, and checksums | `python3 validation/check_package.py` | PASS, 36 files |
| Publication figure | PNG signature, dimensions, inventory, and checksum | PASS, 1600 x 900 |
| Required terminal empty records | `python3 validation/check_blank.py data/b049230.txt data/b033323.txt data/certified_terms.tsv` | PASS |
| Complete quick runner | `sh validation/run.sh quick` | PASS |
| Frozen Makefile bounded regression | `make -C src test` | PASS in disposable `n=8` states |
| Final manuscript source, two draft-mode passes | `sh validation/run.sh paper` | PASS; no warning or error |
| Final rendered manuscript | two normal `pdflatex` passes, Poppler inspection, and page rendering | PASS; 10 pages, no warning or visual defect |
| Manuscript publication policy | bundled deterministic policy audit | PASS, 13/13 gates |
| Epistemic chronology | chronology ledger, pre-event test, and cross-section audit | `EPISTEMIC CHRONOLOGY PASS` |
| Citation YAML parse | Python `yaml.safe_load` plus field assertions | PASS |
| Citation CFF schema | `cffconvert --validate --infile CITATION.cff` | PASS, CFF 1.2.0 |
| License and third-party boundary | root `LICENSE`, `NOTICE.md`, CFF metadata | PASS; MIT original work, axial data CC BY 4.0 |
| Public jOEIS full reproduction | `sh validation/run.sh joeis` with explicit JDK paths | PASS, exact row `19 2126459849880`; verifier 3189 s |
| Frozen source/build identity | byte comparison with reviewed/run artifacts | PASS |
| Canonical workspace/public b-file identity | byte comparison | PASS |
| Campaign state/result identity | byte comparison | PASS |
| Filename budget | every public basename at most 25 characters | PASS |
| Public locators | GitHub HTTP response and Zenodo DOI/API resolution | PASS; repository public, stable concept DOI verified |
| Publication boundary | no review directories, local absolute paths, or generated executable | PASS; final TeX and PDF included intentionally |

The quick runner built the copied C source, reproduced `p3(1..8)`, matched the
direct and explicit-orbit Python oracles, audited the frozen public inventory,
verified the completed campaign state read-only (`2411/2411` units,
`n=1..22`), replayed the prior prefix and Crippen rows, checked all published
checkpoints through `n=27`, aggregated the hash-pinned axial data, and removed
the generated executable.

## Numerical summary

The raw campaign result ends with:

```text
22 4011450432806
```

This is `p3(22)`. Exact reconstruction with `A033323(22)=1236865976` gives:

```text
A049230(22) = 3*1236865976 + 48*4011450432806
             = 192553331372616
```

The canonical A049230 b-file ends with the exact published row:

```text
27 322845646222032480
```

The b-file contains 27 consecutive data rows and ends with final bytes
`0a 0a`. `data/b033323.txt` supplies the independently maintained planar input
through `n=59` and has the same required terminal framing.

## Production identity and telemetry

The frozen production command was initialized with maximum length 22, 8
threads, split depth 10, unit size 64, and a 4500-second soft segment target.
The public `validation/run/manifest.tsv` contains all 2411 recovery-unit rows
and the final integrity record. The authoritative accumulated per-unit wall
time is 10232.08 seconds.

## Independent-validator inventory

| Validator | Class | Public status |
| --- | --- | --- |
| `derive_a049230.py` | independent exact arithmetic and source normalization | Executed in quick runner; PASS through published `n=27` |
| `direct_oracle.py` | bounded independent Python enumeration | Executed through `n=8`; PASS |
| `orbit_oracle.py` | bounded explicit 48-image orbit enumeration | Executed through `n=8`; PASS |
| `check_axial.py` | independent aggregation of cited primary data | Executed at `n=19,20`; PASS |
| built-in `verify` | production state/coverage self-verification | Executed read-only; PASS 2411/2411 |
| `joeis/run_verifier.sh` | independent Java backend, shared exhaustive DFS method | Fresh portable wrapper fetched, hash-checked, compiled, and passed at `n=19` in 3189 verifier seconds |

The jOEIS stage remains separate because it requires network access and about
53 minutes with no checkpoint. The fresh public-wrapper run returned exact
stdout `19 2126459849880`. The public repository
`https://github.com/carcorti/A049230` returned HTTP 200. The stable concept DOI
`10.5281/zenodo.22171393` resolved to the current published Zenodo record
`10.5281/zenodo.22171394`, whose metadata identify release `v1.0`. The local
package is the final candidate for GitHub release `v1.0.1`; the pre-trigger
version difference is expected, and the GitHub release will initiate the
corresponding Zenodo synchronization.

## Core SHA-256 identities

| Artifact | SHA-256 |
| --- | --- |
| `src/a049230.c` | `9c2ccb6305b9bef7b6b181e52a7b4543cefb17374a3bed4ca74bcbd5f9fce6ca` |
| `src/Makefile` | `89492cd2fce5edb96d3a2c64379b6388ddd17b861d93450c5de60944ff5fb408` |
| `data/b049230.txt` | `2f68f3cfc3087c96be288269d4a00b7fe93d5812079824c7692208e8d32fdea4` |
| `data/b033323.txt` | `088ebea2b7bc0b3c396534bbd0702e7f1c430bca823ec32f8a9706b77ccb0edf` |
| `data/certified_terms.tsv` | `584b65f100e1b8e8e5c28982bb387972765cab70564331d15f3f00eb94970bd3` |
| `results/a049230.tsv` | `0162b478db92a8955bd8cbbc8bfefdac83ea453974331c051610b221e19e254a` |
| `results/p3_orbits.tsv` | `45760feb34dd63d302ac8ed063e7951259dbcc895709c1196a2979d8feb9e8d1` |
| `validation/run/manifest.tsv` | `f61f66585008df06ac63a094eeff4cfb010782a108debe3636bcff1b9e7d4524` |
| `validation/run/results.tsv` | `0a5d2b21a8c66cd7c8d9f8b3b2d8a37216436b8038550a9abc44d18dea8f43d1` |
| `validation/axial_data.txt` | `5924996f048830b76a81854c33cbbcbc6a29b13600b3ea7e4e59db0fe6908e78` |
| `figures/A049230_infographic.png` | `617b0d7ce2e7f7a5f0b0e60189eddd07680270c96c09458db69f9d67cb69f772` |
| `validation/joeis/Animal.java` | `705d0471baf3d65a9b1ca7be80a740f87ed6ad95ce2081818bd01b1211be61b2` |
| `validation/joeis/A049230Verifier.java` | `ef37a73f90128b58a5ae60d0802a0e4cb6745ecfa497dddf01ad3070590e8d54` |
| `validation/joeis/run_verifier.sh` | `43fc8a145d6c7e58981e62f2afc1f5e1fcdd7b8f8fa7897f9b4c8e1f4a6145cf` |
| `paper/A049230.tex` | `9f1a8bf80154b6614629cb6006092ca828a278b7fedb8976eed13577da634509` |
| `paper/A049230.pdf` | `535ca2ed05f3cc95f64e583abf91692841cebe6da43023adbeb534de35ffccd5` |
| `CITATION.cff` | `3643450237a906ed5e07346b4761f312984dcbdf28703700d3bbce2b9e874c7d` |

The complete public-tree inventory is recorded in `checksums.sha256`, excluding
that checksum file itself. Any later file change invalidates the affected row
and requires a fresh validation/checksum sweep.

## Remaining external publication actions

- publish GitHub release `v1.0.1` from these frozen package bytes;
- confirm that the automatic Zenodo synchronization creates the corresponding
  release and preserves concept DOI `10.5281/zenodo.22171393`.
