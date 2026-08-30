#!/usr/bin/env python3
"""Independent bounded oriented-walk oracle for p_(n,2)^(3), n <= 8."""

from __future__ import annotations

import argparse


DIRECTIONS = (
    (1, 0, 0), (-1, 0, 0),
    (0, 1, 0), (0, -1, 0),
    (0, 0, 1), (0, 0, -1),
)


def enumerate_oriented(max_n: int) -> list[int]:
    raw = [0] * (max_n + 1)
    occupied = {(0, 0, 0)}

    def visit(
        position: tuple[int, int, int],
        steps: int,
        contacts: int,
        axis_mask: int,
    ) -> None:
        if contacts == 2 and axis_mask == 0b111:
            raw[steps] += 1
        if steps == max_n:
            return
        x, y, z = position
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
            visit(candidate, steps + 1, next_contacts, next_mask)
            occupied.remove(candidate)

    visit((0, 0, 0), 0, 0, 0)
    return raw


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--max-n", type=int, default=8)
    arguments = parser.parse_args()
    if not 1 <= arguments.max_n <= 8:
        parser.error("--max-n must be between 1 and 8")

    raw = enumerate_oriented(arguments.max_n)
    for n in range(1, arguments.max_n + 1):
        if raw[n] % 48:
            raise RuntimeError(f"oriented count at n={n} is not divisible by 48")
        print(n, raw[n] // 48, raw[n])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
