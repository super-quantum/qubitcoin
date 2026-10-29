#!/usr/bin/env python3
# Copyright (c) 2025 Superquantum
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""deqode (proof-of-work task 1) for the functional tests.

An independent implementation of the instance derivation in src/pow/tasks/deqode.cpp,
plus the witness encoding and a Prange solver. See doc/deqode.md.
"""

import hashlib
import struct

from test_framework.messages import ser_uint256

CODE_TAG = "QubitcoinDeqode/code"
SYNDROME_TAG = "QubitcoinDeqode/syndrome"


class HashStream:
    """SHA256(SHA256(tag) || SHA256(tag) || seed || counter) in counter mode, read as
    big-endian 64-bit words. The 64-bit big-endian counter starts at 1."""

    def __init__(self, tag, seed):
        tag_hash = hashlib.sha256(tag.encode("ascii")).digest()
        self.prefix = tag_hash + tag_hash
        self.seed = bytes(seed)
        self.counter = 0
        self.used = 4
        self.block = b""

    def next(self):
        if self.used == 4:
            self.counter += 1
            self.used = 0
        if self.used == 0:
            self.block = hashlib.sha256(
                self.prefix + self.seed + self.counter.to_bytes(8, "big")
            ).digest()
        word = int.from_bytes(self.block[8 * self.used:8 * self.used + 8], "big")
        self.used += 1
        return word


class BitReservoir:
    """Bits from a HashStream, low end first. A draw wider than what is left discards
    the remainder and starts a fresh word."""

    def __init__(self, tag, seed):
        self.stream = HashStream(tag, seed)
        self.reservoir = 0
        self.available = 0

    def take(self, width):
        if width <= 0:
            return 0
        if self.available < width:
            self.reservoir = self.stream.next()
            self.available = 64
        value = self.reservoir & ((1 << width) - 1)
        self.reservoir >>= width
        self.available -= width
        return value

    def below(self, bound):
        """Uniform on [0, bound), by rejection sampling."""
        width = 0
        while (1 << width) < bound:
            width += 1
        while True:
            value = self.take(width)
            if value < bound:
                return value

    def gate(self, n):
        """(gate, first, second): 0 is H and 1 is S on first, 2 is CNOT first -> second."""
        gate = self.below(3)
        first = self.below(n)
        second = 0
        if gate == 2:
            second = self.below(n - 1)
            if second >= first:
                second += 1
        return (gate, first, second)


def derive_columns(n, r, seed, gate_multiplier):
    """Run the Clifford circuit; return the X and Z columns as r-bit integers."""
    reservoir = BitReservoir(CODE_TAG, seed)
    col_x = [0] * n
    col_z = [0] * n
    for row in range(r):
        col_z[row] |= 1 << row
    for _ in range(gate_multiplier * n):
        gate, first, second = reservoir.gate(n)
        if gate == 0:
            col_x[first], col_z[first] = col_z[first], col_x[first]
        elif gate == 1:
            col_z[first] ^= col_x[first]
        else:
            col_x[second] ^= col_x[first]
            col_z[first] ^= col_z[second]
    return col_x, col_z


def derive_syndrome(r, seed):
    """An r-bit syndrome, as an integer."""
    stream = HashStream(SYNDROME_TAG, seed)
    value = 0
    for index in range((r + 63) // 64):
        value |= stream.next() << (64 * index)
    return value & ((1 << r) - 1)


def header_prefix80(header):
    """The first 80 bytes of the header, without the task data."""
    return (struct.pack("<i", header.nVersion)
            + ser_uint256(header.hashPrevBlock)
            + ser_uint256(header.hashMerkleRoot)
            + struct.pack("<I", header.nTime)
            + struct.pack("<I", header.nBits)
            + struct.pack("<I", header.nNonce))


def instance_from_header(header, sizing):
    """(col_x, col_z, syndrome) for a header. The code is seeded by hashPrevBlock and
    the syndrome by SHA256 of the 80-byte header."""
    col_x, col_z = derive_columns(sizing["n"], sizing["r"],
                                  ser_uint256(header.hashPrevBlock),
                                  sizing["gate_multiplier"])
    syn = derive_syndrome(sizing["r"], hashlib.sha256(header_prefix80(header)).digest())
    return col_x, col_z, syn


def syndrome(col_x, col_z, x_mask, z_mask, n):
    """An X error on qubit q flips the rows in col_z[q], a Z error those in col_x[q]."""
    out = 0
    for q in range(n):
        if (x_mask >> q) & 1:
            out ^= col_z[q]
        if (z_mask >> q) & 1:
            out ^= col_x[q]
    return out


def pauli_weight(x_mask, z_mask):
    return bin(x_mask | z_mask).count("1")


def encode_witness(x_mask, z_mask, n):
    """2 * ceil(n/8) bytes: the X mask then the Z mask, little-endian."""
    width = (n + 7) // 8
    return x_mask.to_bytes(width, "little") + z_mask.to_bytes(width, "little")


def solve(col_x, col_z, target_syndrome, n, w, rng, max_iterations=20000):
    """Prange information-set decoding; returns (x_mask, z_mask) or None."""
    # Column q is an X on qubit q, column n + q a Z.
    generators = list(col_z) + list(col_x)
    order = list(range(2 * n))

    for _ in range(max_iterations):
        rng.shuffle(order)
        pivots = {}  # highest set bit -> (reduced column, error mask)
        for index in order:
            column = generators[index]
            mask = 1 << index
            while column:
                top = column.bit_length() - 1
                if top not in pivots:
                    break
                column ^= pivots[top][0]
                mask ^= pivots[top][1]
            if column:
                pivots[column.bit_length() - 1] = (column, mask)

        residue = target_syndrome
        solution = 0
        while residue:
            top = residue.bit_length() - 1
            if top not in pivots:
                break
            residue ^= pivots[top][0]
            solution ^= pivots[top][1]
        if residue:
            continue

        x_mask = solution & ((1 << n) - 1)
        z_mask = (solution >> n) & ((1 << n) - 1)
        if pauli_weight(x_mask, z_mask) <= w:
            return x_mask, z_mask

    return None
