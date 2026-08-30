#!/usr/bin/env python3
"""Explicit 48-image orbit oracle for p_(n,2)^(3), intentionally n <= 8."""

from __future__ import annotations

import argparse
from itertools import permutations, product


DIRECTIONS = (
    (1, 0, 0), (-1, 0, 0),
    (0, 1, 0), (0, -1, 0),
    (0, 0, 1), (0, 0, -1),
)
GROUP = tuple(product(permutations(range(3)), product((1, -1), repeat=3)))
assert len(GROUP) == 48 and len(set(GROUP)) == 48


def apply(operation, point):
    permutation, signs = operation
    return tuple(signs[i] * point[permutation[i]] for i in range(3))


def is_canonical(walk) -> bool:
    if len(walk) < 2 or walk[1] != (1, 0, 0):
        return False
    seen_positive_y = False
    seen_positive_z = False
    for _, y, z in walk[2:]:
        if y < 0 and not seen_positive_y:
            return False
        if z != 0 and not seen_positive_y:
            return False
        if z < 0 and not seen_positive_z:
            return False
        seen_positive_y = seen_positive_y or y > 0
        seen_positive_z = seen_positive_z or z > 0
    return seen_positive_z


def enumerate_good(n: int) -> set[tuple[tuple[int, int, int], ...]]:
    good: set[tuple[tuple[int, int, int], ...]] = set()
    walk = [(0, 0, 0)]
    occupied = {(0, 0, 0)}

    def visit(steps: int, contacts: int, axis_mask: int) -> None:
        if steps == n:
            if contacts == 2 and axis_mask == 0b111:
                good.add(tuple(walk))
            return
        x, y, z = walk[-1]
        for dx, dy, dz in DIRECTIONS:
            candidate = (x + dx, y + dy, z + dz)
            if candidate in occupied:
                continue
            occupied_neighbours = sum(
                (candidate[0] + ax, candidate[1] + ay, candidate[2] + az)
                in occupied
                for ax, ay, az in DIRECTIONS
            )
            next_contacts = contacts + occupied_neighbours - 1
            if next_contacts > 2:
                continue
            next_mask = axis_mask
            if dx:
                next_mask |= 0b001
            if dy:
                next_mask |= 0b010
            if dz:
                next_mask |= 0b100
            occupied.add(candidate)
            walk.append(candidate)
            visit(steps + 1, next_contacts, next_mask)
            walk.pop()
            occupied.remove(candidate)

    visit(0, 0, 0)
    return good


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--max-n", type=int, default=8)
    arguments = parser.parse_args()
    if not 1 <= arguments.max_n <= 8:
        parser.error("--max-n must be between 1 and 8")

    for n in range(1, arguments.max_n + 1):
        good = enumerate_good(n)
        unseen = set(good)
        orbit_count = 0
        while unseen:
            walk = next(iter(unseen))
            orbit = {
                tuple(apply(operation, point) for point in walk)
                for operation in GROUP
            }
            if len(orbit) != 48:
                raise RuntimeError(f"n={n}: orbit size {len(orbit)} != 48")
            if not orbit <= good:
                raise RuntimeError(f"n={n}: group action leaves the target set")
            canonical_count = sum(is_canonical(image) for image in orbit)
            if canonical_count != 1:
                raise RuntimeError(
                    f"n={n}: canonical representatives per orbit={canonical_count}"
                )
            unseen -= orbit
            orbit_count += 1
        if len(good) != 48 * orbit_count:
            raise RuntimeError(f"n={n}: oriented/orbit normalization failure")
        print(n, orbit_count, len(good))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
