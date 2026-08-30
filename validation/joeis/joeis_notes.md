# Independent jOEIS verifier

This directory contains the public, bounded jOEIS verifier for `A049230(19)`.
It directly enumerates the complete oriented/reflected count with Sean A.
Irvine's `ExactContactsWalker`. It does not read the C campaign result,
`p3_orbits.tsv`, A033323, the Python derivator, or the b-file.

The Java implementation is independent of the production program in author,
language, coordinate representation, occupancy lookup, symmetry accounting,
parallel structure, compilation, and result path. It is not a different
mathematical method: both implementations use exhaustive self-avoiding-walk
enumeration with incremental contact counting.

`run_verifier.sh` downloads seven jOEIS source files from pinned commit
`b850b460d2110ec2860f3bc77289f4632937d8de`, verifies their SHA-256 values,
compiles in a private temporary directory, and removes the directory on exit.
`Animal.java` is a type-only compile stub; `A049230Verifier.java` is the strict
single-index harness and three-branch scheduler.

Requirements are `curl`, `sha256sum`, and a JDK providing `java` and `javac` on
`PATH`, or executable paths supplied through `JAVA` and `JAVAC`.

From the repository root, the full public check is:

```sh
sh validation/run.sh joeis
```

Expected stdout is exactly:

```text
19 2126459849880
```

Carlo executed the frozen pre-publication launcher on 26 August 2026. It exited
0 and returned that integer after 3084 seconds of verifier time (51 min 26.56 s
end-to-end), with 147180 KiB peak end-to-end RSS. The public launcher differs
only by removal of a workstation-specific JDK fallback path; the two Java
sources and the hash-pinned upstream source closure are unchanged. The full
public-wrapper stage was rerun on 26 August 2026. It fetched and verified the
pinned source closure, compiled successfully, exited 0, returned the exact row
`19 2126459849880`, and recorded `elapsed_s=3189` for the Java verifier. It
remains a long, network-dependent run with no checkpoint.

The portable public wrapper was smoke-tested separately at `n=1` with explicit
`JAVA` and `JAVAC` paths. It fetched and verified the pinned source closure,
compiled successfully, exited 0, and returned the exact row `1 0`.

The result establishes implementation independence at `n=19` only. It does
not independently enumerate `n=20..22` and must not be described as a distinct
mathematical method.
