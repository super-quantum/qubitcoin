#!/usr/bin/env python3
# Copyright (c) 2025 Superquantum
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Print a deqode hardness table for chainparams.

The cost model is the expected number of Prange iterations per witness of weight at
most w, 2^r / sum(C(r, k) for k <= w). It ignores n and better decoders, and is only
meant for test sizings such as regtest's.

Usage:

    contrib/devtools/deqode-hardness.py --n 32 --r 17 --w-min 2 --w-max 8 [--anchor-w 4 --anchor-k 10]
"""

import argparse
from math import comb

# Must track DEQODE_K_MIN / DEQODE_K_MAX in src/pow/tasks/deqode.h.
K_MIN = 4
K_MAX = 92


def to_compact(value):
    """arith_uint256::GetCompact."""
    assert value > 0
    nsize = (value.bit_length() + 7) // 8
    if nsize <= 3:
        compact = value << (8 * (3 - nsize))
    else:
        compact = value >> (8 * (nsize - 3))
    if compact & 0x00800000:
        compact >>= 8
        nsize += 1
    return compact | (nsize << 24)


def from_compact(compact):
    """arith_uint256::SetCompact."""
    nsize = compact >> 24
    word = compact & 0x007FFFFF
    if nsize <= 3:
        return word >> (8 * (3 - nsize))
    return word << (8 * (nsize - 3))


def prange_cost(r, w):
    """Expected reference-solver iterations per witness at weight bound w."""
    successes = sum(comb(r, k) for k in range(0, min(w, r) + 1))
    return round(2 ** r / successes)


def bits_for_difficulty(difficulty):
    """nBits for (about) the given difficulty."""
    return to_compact((2 ** 256) // difficulty - 1)


def difficulty_for_bits(bits):
    target = from_compact(bits)
    return (2 ** 256) // (target + 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--n", type=int, required=True, help="qubits")
    parser.add_argument("--r", type=int, required=True, help="stabilizer generators")
    parser.add_argument("--w-min", type=int, required=True, help="dearest weight bound tabulated")
    parser.add_argument("--w-max", type=int, required=True, help="cheapest weight bound tabulated")
    parser.add_argument("--gate-multiplier", type=int, default=24)
    parser.add_argument("--anchor-w", type=int, default=None,
                        help="if given, also print an nInitialBits landing on this weight bound")
    parser.add_argument("--anchor-k", type=int, default=None,
                        help="the expected witnesses per block that anchor should imply")
    args = parser.parse_args()

    if not 0 <= args.w_min <= args.w_max <= args.n:
        parser.error("need 0 <= w_min <= w_max <= n")

    weights = list(range(args.w_min, args.w_max + 1))
    costs = [prange_cost(args.r, w) for w in weights]
    encoded = [to_compact(c) for c in costs]
    decoded = [from_compact(e) for e in encoded]

    for i in range(1, len(decoded)):
        if not decoded[i] < decoded[i - 1]:
            parser.error(f"table is not strictly decreasing at w={weights[i]}: "
                         f"{decoded[i - 1]} -> {decoded[i]}")
        if decoded[i - 1] > decoded[i] * (K_MAX // K_MIN):
            parser.error(f"step into w={weights[i]} is wider than the difficulty band: "
                         f"{decoded[i - 1]} / {decoded[i]} > {K_MAX // K_MIN}")

    print(f"                    // contrib/devtools/deqode-hardness.py --n {args.n} --r {args.r} --w-min {args.w_min} --w-max {args.w_max}")
    print("                    .hardness = {")
    for w, enc in zip(weights, encoded):
        print(f"                        0x{enc:08x}, // w = {w}")
    print("                    },")

    if args.anchor_w is not None:
        if args.anchor_w not in weights:
            parser.error("--anchor-w is outside the tabulated range")
        i = weights.index(args.anchor_w)
        k = args.anchor_k if args.anchor_k is not None else K_MIN
        # w is chosen when K_MIN * C[w] <= difficulty < K_MIN * C[w - 1].
        low = K_MIN * decoded[i]
        high = K_MIN * decoded[i - 1] if i > 0 else None
        want = k * decoded[i]
        if want < low or (high is not None and want >= high):
            parser.error(f"K = {k} does not land on w = {args.anchor_w}: difficulty must be "
                         f"in [{low}, {high if high is not None else 'inf'})")
        bits = bits_for_difficulty(want)
        got = difficulty_for_bits(bits)
        print()
        print(f"                .nInitialBits = 0x{bits:08x}, // difficulty {got}: w = {args.anchor_w}, K = {got // decoded[i]}")


if __name__ == "__main__":
    main()
