#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
  echo "usage: $0 N serial|split3" >&2
  exit 2
fi

case "$1" in
  ''|*[!0-9]*|0|0*)
    echo "N must use canonical decimal notation in 1..19" >&2
    exit 2
    ;;
esac
if [ "$1" -gt 19 ]; then
  echo "N must be in 1..19" >&2
  exit 2
fi
case "$2" in
  serial|split3) ;;
  *)
    echo "mode must be serial or split3" >&2
    exit 2
    ;;
esac

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ -n "${JAVAC:-}" ]; then
  javac_bin=$JAVAC
elif command -v javac >/dev/null 2>&1; then
  javac_bin=$(command -v javac)
else
  echo "no javac found; set JAVAC to an executable JDK compiler" >&2
  exit 1
fi

if [ -n "${JAVA:-}" ]; then
  java_bin=$JAVA
elif [ -x "$(dirname -- "$javac_bin")/java" ]; then
  java_bin=$(dirname -- "$javac_bin")/java
elif command -v java >/dev/null 2>&1; then
  java_bin=$(command -v java)
else
  echo "no java runtime found; set JAVA to an executable runtime" >&2
  exit 1
fi

if [ ! -x "$javac_bin" ] || [ ! -x "$java_bin" ]; then
  echo "JAVA/JAVAC must name executable files" >&2
  exit 1
fi
if ! command -v curl >/dev/null 2>&1; then
  echo "curl is required to retrieve the pinned jOEIS sources" >&2
  exit 1
fi

build_dir=$(mktemp -d /tmp/a049230-joeis.XXXXXX)
case "$build_dir" in
  /tmp/a049230-joeis.*) ;;
  *)
    echo "unexpected temporary directory: $build_dir" >&2
    exit 1
    ;;
esac
cleanup() {
  rm -rf -- "$build_dir"
}
trap cleanup EXIT HUP INT TERM

package_dir=$build_dir/src/irvine/math/lattice
class_dir=$build_dir/classes
mkdir -p "$package_dir" "$class_dir"

commit=b850b460d2110ec2860f3bc77289f4632937d8de
base=https://raw.githubusercontent.com/archmageirvine/joeis/$commit/src/irvine/math/lattice

while read -r expected name; do
  curl -fsSL "$base/$name" -o "$package_dir/$name"
  printf '%s  %s\n' "$expected" "$package_dir/$name" \
    | sha256sum --check --strict - >/dev/null
done <<'JOEIS_SOURCES'
bccddc1fad67f9523eeb3c96b58047cbc38db6bce9d5a73ece34405b319cf0ce Accumulator.java
727f03ab84afd0936c8e7c0b6804b57818a63f2888b1cf8f0e4a3b563967efa4 Walker.java
74cbac9f7eed05b92151514e312b34c5fd826384380f01494925f654ff262e3d SelfAvoidingWalker.java
d9b1ec9a0fc0e2b1893610ca2260c9535a235e87142d37f7b579908ff7fb9df4 ExactContactsWalker.java
2a26eea3d3c8340948315d850977663e35a167cf43a632de45ab4f3d333eacb7 Lattice.java
4eaf64bb85c7034831b2bcf4e51f64f8ab47a6571e3fca695ed8788adf4d81b8 AbstractLattice.java
a6acd5c6fdc32d530d1e9bfc5c1a9b2c6186076daaa8fa619cd8abb3ab230505 CubicLattice.java
JOEIS_SOURCES

cp -- "$script_dir/Animal.java" "$package_dir/Animal.java"
cp -- "$script_dir/A049230Verifier.java" "$package_dir/A049230Verifier.java"

printf '%s  %s\n' \
  '705d0471baf3d65a9b1ca7be80a740f87ed6ad95ce2081818bd01b1211be61b2' "$package_dir/Animal.java" \
  'ef37a73f90128b58a5ae60d0802a0e4cb6745ecfa497dddf01ad3070590e8d54' "$package_dir/A049230Verifier.java" \
  | sha256sum --check --strict - >/dev/null

"$javac_bin" -encoding UTF-8 -d "$class_dir" \
  -sourcepath "$build_dir/src" "$package_dir/A049230Verifier.java"

printf 'JOEIS_COMMIT=%s MODE=%s N=%s\n' "$commit" "$2" "$1" >&2
printf 'JAVA_SHA256=' >&2
sha256sum "$java_bin" | awk '{print $1}' >&2
printf 'JAVAC_SHA256=' >&2
sha256sum "$javac_bin" | awk '{print $1}' >&2

"$java_bin" -cp "$class_dir" irvine.math.lattice.A049230Verifier "$1" "$2"
