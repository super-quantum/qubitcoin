# deqode (proof-of-work task 1)

deqode is a proof of work based on decoding random stabilizer codes. Each block
defines a random stabilizer code and a target syndrome; a solution is a Pauli error of
bounded weight with that syndrome. This document specifies the task for miners. See
[pow-tasks.md](pow-tasks.md) for how tasks work in general. The implementation is
`src/pow/tasks/deqode.cpp`, and `qubitcoin-miner` has a CPU miner for it.

## Acceptance rule

The block's task data holds a Pauli error `e` on `n` qubits. The header is valid when:

1. the task data decodes as a witness: exactly `2 * ceil(n/8)` bytes with zero
   padding bits (see *Witness encoding*);
2. `SHA256(prefix80 || e)`, read as a little-endian 256-bit integer, is at most the
   acceptance target;
3. the Pauli weight of `e` is at most `w`;
4. the syndrome of `e` under the block's code equals the block's syndrome.

A block failing any of these is rejected as `high-hash`, and one with no task data at
all as `missing-task-data`.

## Difficulty

`nBits` encodes a difficulty `D = 2^256 / (target + 1)` as for task 0; the RPCs report
difficulty in Bitcoin's units, about `D / 2^32`. `D` is split between the decoding and
the acceptance hash using the task's hardness table `C`, which gives the expected cost
of one witness for each weight bound `w` in `[w_min, w_max]` (compact-encoded,
decreasing in `w`):

```
w      = the smallest w with C[w] * K_MIN <= D
K      = D / C[w]                       (integer division)
target = (2^256 - 1) / K
```

`K` is the expected number of witnesses per block, and `K_MIN = 4`, `K_MAX = 92`. The
table's steps are at most `K_MAX / K_MIN`, so `K` stays in `[K_MIN, K_MAX]` over the
whole range the table covers. If `D < C[w_max] * K_MIN` there is no valid `w` and no
block can be mined at that difficulty.

`nBits` must not encode a target above `powLimit`. The acceptance target is not
compared against `powLimit`.

`w` and the acceptance target change with `nBits`, so a miner has to take them from
each new template.

## Instance derivation

`prefix80` is the first 80 bytes of the header (version, previous block hash, merkle
root, time, bits, nonce), serialized as for task 0. It does not include the task data
or its length prefix.

The code is derived from `hashPrevBlock` and is the same for every nonce. The syndrome
is derived from `SHA256(prefix80)`. Both seeds are used as raw bytes in serialization
order.

### Hash stream

Each derivation reads a SHA-256 stream:

```
block(i) = SHA256( SHA256(tag) || SHA256(tag) || seed || i as 8-byte big-endian )
```

for `i = 1, 2, ...`. Each block gives four 64-bit words, read big-endian from bytes
0–7, 8–15, 16–23 and 24–31. The tags are the ASCII strings `QubitcoinDeqode/code`
and `QubitcoinDeqode/syndrome`.

### Bit reservoir

The code stream is read through a 64-bit reservoir. A draw of `width` bits takes the
low `width` bits and shifts the reservoir right. If fewer than `width` bits are left,
they are discarded and the next word is loaded. `Below(bound)` draws the smallest
`width` with `2^width >= bound` and repeats until the value is below `bound`.

### Code

The code is a stabilizer tableau stored by column: `col_x[q]` and `col_z[q]` are
`r`-bit vectors holding each stabilizer's X and Z bit on qubit `q`. Initially
stabilizer `i` is Z on qubit `i` (bit `i` of `col_z[i]` is set, for `i < r`). Then
`gate_multiplier * n` gates are drawn and applied:

```
gate   = Below(3)          0 = H, 1 = S, 2 = CNOT
first  = Below(n)
second = Below(n - 1)      only for CNOT; if second >= first then second += 1
```

| gate | update |
|---|---|
| H on `f` | swap `col_x[f]` and `col_z[f]` |
| S on `f` | `col_z[f] ^= col_x[f]` |
| CNOT from `f` to `s` | `col_x[s] ^= col_x[f]`, then `col_z[f] ^= col_z[s]` |

### Syndrome

The block's syndrome is `ceil(r/64)` words read directly from the syndrome stream (no
reservoir). Bit `i` is bit `i mod 64` of word `i div 64`, and only the low `r` bits
are used.

The syndrome of an error `e` is the XOR of `col_z[q]` for every qubit where `e` has an
X component and `col_x[q]` for every qubit where it has a Z component (a Y has both).

## Witness encoding

`2 * ceil(n/8)` bytes: the X mask followed by the Z mask, each with qubit `q` in bit
`q mod 8` of byte `q div 8`. Bits for qubits `n` and above must be zero. The Pauli
weight is the number of qubits with a non-identity Pauli, `popcount(x | z)`.

## getblocktemplate

A template for task 1 (`"task_id": 1`) has a `task` object like:

```json
{
  "id": 1,
  "name": "deqode",
  "data_bytes": 72,
  "deqode": {
    "n": 288,
    "r": 187,
    "gate_multiplier": 24,
    "code_seed": "hashPrevBlock",
    "syndrome_seed": "sha256(header[0:80])",
    "acceptance_preimage": "header[0:80] || witness",
    "w": 45,
    "expected_witnesses": 5,
    "acceptance_target": "3333333333333333..."
  }
}
```

`w`, `expected_witnesses` (`K`) and `acceptance_target` (big-endian hex) are derived
from the template's `nBits`. If the difficulty is below what the table covers they are
missing and `task` has `"unsupported": true`. `getmininginfo` also reports
`weight_bound` and `expected_witnesses` for the task.

The solver used by the node's `generate*` RPCs is a simple Prange decoder meant for
regtest. It is far too slow for testnet's sizing.

## Parameters

| | testnet | regtest |
|---|---|---|
| activation | height 5 | `-deqodeheight=<n>`, off by default |
| `n`, `r`, `gate_multiplier` | 288, 187, 24 | 32, 17, 24 |
| `w` range | 31–65 | 2–8 |
| `nInitialBits` | `0x1c233d9a` (difficulty 31200111727) | `0x20009fd8` (difficulty 410) |
| anchor | `w = 45`, `K = 5` | `w = 4`, `K = 10` |
| minimum difficulty | `w = 46`, `K = 4` | `w = 4`, `K = 10` (the anchor) |
| witness bytes | 72 | 8 |

Testnet's hardness table is the measured expected number of candidates a Stern decoder
(window 19, orders 2:2) examines per witness, for `w >= 40`, extrapolated down to
`w = 31`. Regtest's table comes from `contrib/devtools/deqode-hardness.py`. Changing
either table is a hard fork.
