# Campaign and endpoint completion

**Sequence:** OEIS A049230  
**Completion date:** 26 August 2026  
**Campaign endpoint:** `n=22`  
**Validated contiguous endpoint:** `n=27`  
**Status:** complete and consolidated

## Campaign result

The frozen C17/OpenMP enumerator completed the exact genuinely
three-dimensional orbit counts needed for `n=19..22`:

| n | p3(n) | A049230(n) |
| ---: | ---: | ---: |
| 19 | 44296694918 | 2126459849880 |
| 20 | 200002700951 | 9600694064496 |
| 21 | 897692330931 | 43090682243160 |
| 22 | 4011450432806 | 192553331372616 |

The full values use the exact reconstruction
`A049230(n) = 3*A033323(n) + 48*p3(n)`.

## Durable state

The archived manifest contains 154271 deterministic depth-10 prefix tasks in
2411 recovery units, all complete, across three human-operated segments. The
authoritative sum of unrounded unit times is 10232.08 seconds. The manifest's
source and Makefile identities match the public frozen copies, and its raw
result is reproduced as a headed publication table under `results/`.

## Validation closure

- established orbit-count prefix through `n=18`: PASS;
- complete manifest continuity and 2411/2411 units: PASS;
- reconstruction against A033323 and prior A049230 rows: PASS;
- Crippen contact-class totals for `n=5..11`: PASS;
- direct and explicit-orbit Python oracles through `n=8`: PASS;
- axial endpoint-constrained subsets at `n=19,20`: PASS;
- independent jOEIS direct reproduction of `a(19)`: PASS in both the preserved
  operator execution and the fresh full public-wrapper run;
- canonical b-file continuity through `n=27` and final bytes `0a 0a`: PASS.

## Endpoint consolidation

The canonical b-file contains the validated prior prefix `1..18`, campaign
rows `19..22`, and exact normalized published rows `23..27`. The dated complete
literature/data audit found no admissible unrestricted value at `n=28` or
above. Therefore 27 is the documented evidence endpoint and 28 is the first
unresolved index. This conclusion does not authorize another computation.

## Publication state

This directory is a raw GitHub publication package. It has not been committed,
tagged, released, uploaded, or synchronized with Zenodo. The DOI remains the
author-required placeholder until Carlo Corti supplies the verified locator.
