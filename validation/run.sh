#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/.." && pwd)

usage() {
  echo "usage: $0 quick|joeis|paper" >&2
  exit 2
}

[ "$#" -eq 1 ] || usage

case "$1" in
quick)
  test_root=$(mktemp -d /tmp/a049230-public.XXXXXX)
  cleanup() {
    make -C "$root/src" clean >/dev/null 2>&1 || true
    case "$test_root" in
      /tmp/a049230-public.*) rm -rf -- "$test_root" ;;
      *) echo "refusing unsafe cleanup path: $test_root" >&2 ;;
    esac
  }
  trap cleanup EXIT HUP INT TERM

  python3 "$script_dir/check_blank.py" \
    "$root/data/b049230.txt" "$root/data/b033323.txt" \
    "$root/data/certified_terms.tsv"
  python3 "$script_dir/check_package.py"
  python3 "$script_dir/check_data.py"

  make -C "$root/src" all
  "$root/src/a049230" compute --max-n 8 --threads 1 --split-depth 8 \
    > "$test_root/c.tsv"
  sed -n '1,8p' "$script_dir/p3_prefix.tsv" > "$test_root/expected.tsv"
  cmp "$test_root/expected.tsv" "$test_root/c.tsv"

  python3 "$script_dir/direct_oracle.py" --max-n 8 > "$test_root/direct.txt"
  python3 "$script_dir/orbit_oracle.py" --max-n 8 > "$test_root/orbit.txt"
  awk '{print $1, $2}' "$test_root/direct.txt" > "$test_root/direct.tsv"
  awk '{print $1, $2}' "$test_root/orbit.txt" > "$test_root/orbit.tsv"
  cmp "$test_root/expected.tsv" "$test_root/direct.tsv"
  cmp "$test_root/expected.tsv" "$test_root/orbit.tsv"

  "$root/src/a049230" verify --state "$script_dir/run" \
    --p3-file "$script_dir/p3_prefix.tsv"
  python3 "$script_dir/derive_a049230.py" \
    --p3 "$script_dir/run/results.tsv" \
    --expect-p3 "$script_dir/p3_prefix.tsv" \
    --a033323 "$root/data/b033323.txt" \
    --a049230-prefix "$script_dir/prefix18.txt" \
    > "$test_root/derived.txt"
  awk -v OFS='\t' '{print $1, $2}' "$test_root/derived.txt" \
    > "$test_root/derived.tsv"
  tail -n +2 "$root/results/a049230.tsv" > "$test_root/public.tsv"
  cmp "$test_root/public.tsv" "$test_root/derived.tsv"
  python3 "$script_dir/check_axial.py" --input "$script_dir/axial_data.txt"
  echo "PUBLIC QUICK VALIDATION: PASS"
  ;;
joeis)
  test_root=$(mktemp -d /tmp/a049230-joeis-check.XXXXXX)
  cleanup() {
    case "$test_root" in
      /tmp/a049230-joeis-check.*) rm -rf -- "$test_root" ;;
      *) echo "refusing unsafe cleanup path: $test_root" >&2 ;;
    esac
  }
  trap cleanup EXIT HUP INT TERM
  "$script_dir/joeis/run_verifier.sh" 19 split3 > "$test_root/result.tsv"
  printf '%s\n' '19 2126459849880' > "$test_root/expected.tsv"
  cmp "$test_root/expected.tsv" "$test_root/result.tsv"
  echo "PUBLIC JOEIS VALIDATION: PASS"
  ;;
paper)
  command -v pdflatex >/dev/null 2>&1 || {
    echo "pdflatex is required for the paper check" >&2
    exit 1
  }
  test_root=$(mktemp -d /tmp/a049230-paper.XXXXXX)
  cleanup() {
    case "$test_root" in
      /tmp/a049230-paper.*) rm -rf -- "$test_root" ;;
      *) echo "refusing unsafe cleanup path: $test_root" >&2 ;;
    esac
  }
  trap cleanup EXIT HUP INT TERM
  cp "$root/paper/A049230.tex" "$test_root/A049230.tex"
  (cd "$test_root" && pdflatex -interaction=nonstopmode -halt-on-error -draftmode A049230.tex >/dev/null)
  (cd "$test_root" && pdflatex -interaction=nonstopmode -halt-on-error -draftmode A049230.tex >/dev/null)
  if grep -Eq 'undefined|Overfull|Underfull|LaTeX Warning|! ' "$test_root/A049230.log"; then
    echo "paper log contains a warning or error" >&2
    exit 1
  fi
  echo "PUBLIC PAPER VALIDATION: PASS"
  ;;
*)
  usage
  ;;
esac
