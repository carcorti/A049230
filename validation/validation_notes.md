# Validation notes

## Scientific relation and index semantics

A049230 uses the step index `n`, while partition polynomials use the monomer
index `N=n+1`. Counts retain all first-step directions and distinguish reversal.
The campaign program emits genuinely three-dimensional cubic-orbit counts
`p3(n)`. Full counts are reconstructed exactly by
`A049230(n) = 3*A033323(n) + 48*p3(n)`.

The target b-file is contiguous through `n=27`. Rows `1..18` are the validated
historical prefix, rows `19..22` are campaign reconstructions, and rows
`23..27` are exact normalized published data. The planar input is joined by
the mathematical index, never by visual row position.

## Evidence inventory

| Class | Public artifact | Scope and boundary |
| --- | --- | --- |
| Frozen production code | `src/a049230.c`, `src/Makefile` | Byte-identical to the reviewed and executed source/build identity. |
| Completed campaign state | `validation/run/manifest.tsv`, `validation/run/results.tsv` | 2411/2411 units, `p3(1..22)`, read-only built-in verification. |
| Independent arithmetic | `validation/derive_a049230.py` | Replays the old prefix, Crippen `n=5..11`, and published `n=23..27` relations. |
| Direct bounded oracle | `validation/direct_oracle.py` | Oriented enumeration through `n=8`, divided only after the exact 48 divisibility check. |
| Explicit orbit oracle | `validation/orbit_oracle.py` | Generates all 48 signed coordinate images through `n=8` and requires one canonical representative per orbit. |
| Primary-data marginal | `validation/check_axial.py`, `validation/axial_data.txt` | Aggregates endpoint-constrained Shirai--Sakumichi data at `n=19,20`; these are subsets, not unrestricted totals. |
| Independent backend | `validation/joeis/` | Direct Java reproduction of `a(19)`; implementation-independent, not method-independent. |
| Publication consistency | `validation/check_data.py`, `validation/check_blank.py` | Reconciles b-files, results, certified terms, source/build identities, endpoint, and byte framing. |

The axial input is the unmodified machine-readable attachment to N. C. Shirai
and N. Sakumichi, *Phys. Rev. Lett.* 130 (2023), 148101,
DOI `10.1103/PhysRevLett.130.148101`. It has header `n,r,m,b,W`, 6813 data rows,
and SHA-256
`5924996f048830b76a81854c33cbbcbc6a29b13600b3ea7e4e59db0fe6908e78`.
Its exact axial two-contact sums are `6927757800` at `n=19` and
`28720091852` at `n=20`; multiplication by six gives the all-axis subsets.
The official article page identifies the article and its components as CC BY
4.0. `NOTICE.md` preserves the required attribution and excludes this
third-party attachment from the repository's MIT grant. `check_axial.py`
requires the pinned attachment hash before aggregating any rows.

The publication-facing `.tsv` files under `data/` and `results/` use actual
tab delimiters. `validation/run/results.tsv` and `validation/p3_prefix.tsv`
are byte-frozen inputs using the production program's canonical
space-separated `n value` grammar; their legacy suffix does not redefine that
frozen format.

## Campaign telemetry

`validation/run/manifest.tsv` is the complete durable campaign manifest. It
records the source and Makefile identities, maximum length 22, split depth 10,
8 threads, unit size 64, 154271 tasks, 2411 recovery units, soft segment target
4500 seconds, three operator segments, every unit's elapsed time and per-depth
counts, and its final integrity record. Raw terminal logs are not archived;
the structured manifest and completion report carry the durable telemetry.

The authoritative accumulated per-unit wall time is `10232.08` seconds. The
target workstation was an AMD Ryzen 9 7940HS (8 cores, 16 logical processors)
with 64 GB DDR5 installed RAM, Linux Mint 22.3, GCC 13.3.0, and GNU libgomp.
ThreadSanitizer and system-call tracing were unavailable and were never
reported as passing checks.

## Endpoint discovery

The provider sweep admits exact full counts from the validated prior prefix,
the completed campaign, and the Lee/Hsieh published partition data after exact
normalization. Crippen, the axial attachment, bounded oracles, and jOEIS are
validation providers and do not extend the endpoint. The maximal contiguous
range is `n=1..27`. At `n=28`, A033323 alone is insufficient: an admissible
unrestricted A049230 count or genuinely three-dimensional orbit count is not
available. No new campaign is authorized or needed for this raw package.

## Package boundary

External reviews, reviewer prompts, local OEIS/editorial material, primary
article PDFs, calibration state, compiled binaries, and transient logs are not
public package artifacts. The provisional TeX source remains versioned. Hashes
and exact paths are retained here as reproducibility metadata and are not
inserted into the scientific manuscript.

The public jOEIS launcher is a portable publication wrapper. It removes only a
workstation-specific JDK fallback path from the executed local launcher. The
Java sources, pinned upstream commit, upstream hashes, CLI contract, and
expected result are unchanged. A fresh full public-wrapper run on 26 August
2026 passed at `n=19` with exact result `2126459849880` and verifier elapsed
time 3189 seconds. The stage remains long, network-dependent, and without a
checkpoint, but is no longer deferred for this raw package.
