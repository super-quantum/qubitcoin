#!/usr/bin/env python3
# Copyright (c) 2026 The Qubitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Unit tests for the functional-test deqode solver; no node is required."""

import random
import unittest

from test_framework.deqode import (
    derive_columns,
    derive_syndrome,
    encode_witness,
    pauli_weight,
    solve,
    syndrome,
)


class OrderedRng:
    """Supply a fixed column order and count information-set iterations."""

    def __init__(self, order):
        self.order = order
        self.calls = 0

    def shuffle(self, values):
        values[:] = self.order
        self.calls += 1


class TestDeqodeSolver(unittest.TestCase):
    def test_full_rank_preserves_first_basis(self):
        rng = OrderedRng([3, 4, 5, 0, 1, 2])
        result = solve([3, 5, 6], [1, 2, 4], 7, 3, 3, rng, 1)
        self.assertEqual(result, (1, 3))
        self.assertEqual(encode_witness(*result, 3), b'\x01\x03')
        self.assertEqual(rng.calls, 1)

    def test_gapped_pivot_positions(self):
        rng = OrderedRng(list(range(8)))
        self.assertEqual(solve([0] * 4, [1, 8, 9, 0], 9, 4, 2, rng, 1), (3, 0))
        self.assertEqual(rng.calls, 1)

    def test_rank_deficient_with_all_coordinate_bits_present(self):
        rng = OrderedRng(list(range(8)))
        self.assertIsNone(solve([0] * 4, [3, 12, 15, 0], 5, 4, 4, rng, 7))
        self.assertEqual(rng.calls, 7)

    def test_zero_rank_and_empty_generators(self):
        for n in (0, 4):
            rng = OrderedRng(list(range(2 * n)))
            self.assertEqual(solve([0] * n, [0] * n, 0, n, 0, rng, 3), (0, 0))
            self.assertEqual(rng.calls, 1)
            rng = OrderedRng(list(range(2 * n)))
            self.assertIsNone(solve([0] * n, [0] * n, 1, n, n, rng, 3))
            self.assertEqual(rng.calls, 3)

    def test_zero_budget_does_not_shuffle(self):
        rng = OrderedRng([0, 1])
        self.assertIsNone(solve([0], [1], 0, 1, 0, rng, 0))
        self.assertEqual(rng.calls, 0)

    def test_rng_consumption_on_success_and_budget_exhaustion(self):
        for target, budget, expected_iterations in ((0, 7, 1), (2, 7, 7), (0, 0, 0)):
            actual_rng = random.Random(17)
            expected_rng = random.Random(17)
            order = [0, 1]
            for _ in range(expected_iterations):
                expected_rng.shuffle(order)
            solve([0], [1], target, 1, 1, actual_rng, budget)
            self.assertEqual(actual_rng.getstate(), expected_rng.getstate())

    def test_generated_columns_return_valid_witnesses(self):
        for n, r in ((8, 4), (16, 9), (32, 17)):
            for seed in range(4):
                with self.subTest(n=n, r=r, seed=seed):
                    col_x, col_z = derive_columns(n, r, seed.to_bytes(32, 'little'), 24)
                    target = derive_syndrome(r, seed.to_bytes(32, 'big'))
                    result = solve(col_x, col_z, target, n, n, random.Random(seed), 1)
                    self.assertIsNotNone(result)
                    self.assertEqual(syndrome(col_x, col_z, *result, n), target)
                    self.assertLessEqual(pauli_weight(*result), n)
                    self.assertEqual(len(encode_witness(*result, n)), 2 * ((n + 7) // 8))


if __name__ == '__main__':
    unittest.main()
