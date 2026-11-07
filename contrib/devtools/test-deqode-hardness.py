#!/usr/bin/env python3
# Copyright (c) 2026 The Qubitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Offline CLI regression tests for deqode-hardness.py; no node is required."""

from pathlib import Path
import subprocess
import sys
import unittest

TOOL = Path(__file__).with_name("deqode-hardness.py")
BASE = ["--n", "32", "--r", "17", "--w-min", "2", "--w-max", "8"]
REGTEST_TABLE = (
    "                    // contrib/devtools/deqode-hardness.py --n 32 --r 17 --w-min 2 --w-max 8\n"
    "                    .hardness = {\n"
    "                        0x02035300, // w = 2\n"
    "                        0x02009d00, // w = 3\n"
    "                        0x01290000, // w = 4\n"
    "                        0x010e0000, // w = 5\n"
    "                        0x01060000, // w = 6\n"
    "                        0x01030000, // w = 7\n"
    "                        0x01020000, // w = 8\n"
    "                    },\n"
)


class HardnessCLITest(unittest.TestCase):
    def run_tool(self, *extra):
        return subprocess.run(
            [sys.executable, str(TOOL), *BASE, *extra],
            capture_output=True, text=True, check=False, timeout=10,
        )

    def assert_invalid(self, expected, *extra):
        result = self.run_tool(*extra)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(result.stdout, "", "Invalid parameters emitted chainparams")
        self.assertIn(expected, result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_default_table(self):
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertEqual(result.stdout, REGTEST_TABLE)

    def test_documented_anchor(self):
        result = self.run_tool("--anchor-w", "4", "--anchor-k", "10")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, REGTEST_TABLE + "\n"
                         "                .nInitialBits = 0x20009fd8, // difficulty 410: w = 4, K = 10\n")

    def test_default_anchor_k(self):
        result = self.run_tool("--anchor-w", "4")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, REGTEST_TABLE + "\n"
                         "                .nInitialBits = 0x20018f9c, // difficulty 164: w = 4, K = 4\n")

    def test_minimum_dimensions(self):
        result = self.run_tool("--n", "2", "--r", "1", "--w-min", "0", "--w-max", "0")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("0x01020000, // w = 0", result.stdout)

    def test_maximum_qubits(self):
        result = self.run_tool("--n", "1024", "--w-min", "4", "--w-max", "4")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--n 1024 --r 17", result.stdout)

    def test_rank_equal_to_qubits(self):
        result = self.run_tool("--r", "32", "--w-min", "4", "--w-max", "4")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--n 32 --r 32", result.stdout)

    def test_positive_gate_multiplier(self):
        result = self.run_tool("--gate-multiplier", "1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, REGTEST_TABLE)

    def test_anchor_k_above_band_at_hardest_weight(self):
        # K_MAX bounds table step sizes, not K at the hardest weight.
        result = self.run_tool("--anchor-w", "2", "--anchor-k", "93")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, REGTEST_TABLE + "\n"
                         "                .nInitialBits = 0x1f00d3fc, // difficulty 79143: w = 2, K = 93\n")

    def test_negative_qubits(self):
        self.assert_invalid("need 1 < n <= 1024", "--n", "-1")

    def test_zero_qubits(self):
        self.assert_invalid("need 1 < n <= 1024", "--n", "0", "--w-min", "0", "--w-max", "0")

    def test_one_qubit(self):
        self.assert_invalid("need 1 < n <= 1024", "--n", "1", "--w-min", "0", "--w-max", "0")

    def test_qubits_above_limit(self):
        self.assert_invalid("need 1 < n <= 1024", "--n", "1025")

    def test_negative_rank(self):
        self.assert_invalid("need 0 < r <= n", "--r", "-1")

    def test_zero_rank(self):
        self.assert_invalid("need 0 < r <= n", "--r", "0", "--w-min", "0", "--w-max", "0")

    def test_rank_above_qubits(self):
        self.assert_invalid("need 0 < r <= n", "--r", "33")

    def test_negative_gate_multiplier(self):
        self.assert_invalid("--gate-multiplier must be positive", "--gate-multiplier", "-1")

    def test_zero_gate_multiplier(self):
        self.assert_invalid("--gate-multiplier must be positive", "--gate-multiplier", "0")

    def test_anchor_k_requires_weight(self):
        self.assert_invalid("--anchor-k requires --anchor-w", "--anchor-k", "10")

    def test_anchor_below_table(self):
        self.assert_invalid("--anchor-w is outside the tabulated range", "--anchor-w", "1")

    def test_anchor_above_table(self):
        self.assert_invalid("--anchor-w is outside the tabulated range", "--anchor-w", "9")

    def test_anchor_k_below_minimum(self):
        self.assert_invalid("K = 3 does not land", "--anchor-w", "4", "--anchor-k", "3")

    def test_anchor_k_outside_weight_band(self):
        self.assert_invalid("K = 1000 does not land", "--anchor-w", "4", "--anchor-k", "1000")

    def test_negative_weight(self):
        self.assert_invalid("need 0 <= w_min <= w_max <= n", "--w-min", "-1")

    def test_reversed_weights(self):
        self.assert_invalid("need 0 <= w_min <= w_max <= n", "--w-min", "8", "--w-max", "2")

    def test_weight_above_qubits(self):
        self.assert_invalid("need 0 <= w_min <= w_max <= n", "--w-max", "33")

    def test_nondecreasing_table(self):
        self.assert_invalid("table is not strictly decreasing", "--r", "1", "--w-min", "1", "--w-max", "2")

    def test_table_step_too_wide(self):
        self.assert_invalid("wider than the difficulty band", "--n", "50", "--r", "50", "--w-min", "0", "--w-max", "1")

    def test_noninteger_argument(self):
        self.assert_invalid("invalid int value", "--n", "not-a-number")


if __name__ == "__main__":
    unittest.main()
