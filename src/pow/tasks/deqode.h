// Copyright (c) 2025 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef QUBITCOIN_POW_TASKS_DEQODE_H
#define QUBITCOIN_POW_TASKS_DEQODE_H

#include <arith_uint256.h>
#include <consensus/params.h>
#include <random.h>
#include <span.h>

#include <cstdint>
#include <vector>

class CBlockHeader;

/**
 * deqode: dense random stabilizer decoding.
 *
 * A block commits to a random stabilizer code and a target syndrome, both derived from
 * the header. A solution is a Pauli error of weight at most w whose syndrome under that
 * code is the target; it is carried in the header's task data. See doc/deqode.md.
 */

/**
 * Bounds on K, the expected number of witnesses per block. Keeping K low keeps the
 * difficulty in the decoding rather than in the acceptance hash. K_MAX / K_MIN is the
 * widest step the hardness table may take.
 *
 * Outside the namespace and constexpr so that ValidatePowTasks, which is built into
 * libbitcoin_common, can use them.
 */
static constexpr uint32_t DEQODE_K_MIN = 4;
static constexpr uint32_t DEQODE_K_MAX = 92;

namespace deqode {

/** A packed bit vector, least significant bit of word 0 being bit 0. */
using BitVector = std::vector<uint64_t>;

/** Largest n this implementation accepts; 2 * ceil(1024/8) is MAX_BLOCK_TASK_DATA_SIZE. */
static constexpr int MAX_DEQODE_QUBITS = 1024;

/** A Pauli error over n qubits: the witness a miner supplies. */
struct Witness {
    BitVector x;
    BitVector z;
};

/**
 * The per-block problem: a stabilizer code in column-major form plus the syndrome to
 * decode. col_x[q] and col_z[q] are r-bit vectors holding each stabilizer's X and Z bit on
 * qubit q. Under the symplectic product an X error on q flips the rows in col_z[q].
 */
struct Instance {
    int n{0};
    int r{0};
    std::vector<BitVector> col_x;
    std::vector<BitVector> col_z;
    BitVector syndrome;
};

/** True if the sizing is one this implementation can derive an instance for. */
bool ParamsAreSane(const Consensus::Params::DeqodeParams& params);

/** The 80 header bytes the derivation and the acceptance hash are built on. */
std::vector<unsigned char> HeaderPrefix80(const CBlockHeader& header);

/** One gate draw off the reservoir. */
struct GateDraw {
    int gate;
    int first;
    int second;
};

/** The first `count` gate draws for a seed (for test vectors). */
std::vector<GateDraw> GateStream(int n, Span<const unsigned char> seed, int count);

/** Run the Clifford circuit for a seed, yielding the code's columns. */
void DeriveColumns(int n, int r, Span<const unsigned char> seed, int gate_multiplier,
                   std::vector<BitVector>& col_x, std::vector<BitVector>& col_z);

/** The r-bit syndrome a seed determines. */
BitVector DeriveSyndrome(int r, Span<const unsigned char> seed);

/** Both halves of the problem a header poses. */
Instance DeriveInstance(const CBlockHeader& header, const Consensus::Params::DeqodeParams& params);

/** The syndrome an error produces under the instance's code. */
BitVector Syndrome(const Instance& instance, const Witness& error);

/** The number of qubits the error acts on non-trivially. */
int PauliWeight(const Witness& error);

/** The canonical 2 * ceil(n/8) byte encoding: the X mask, then the Z mask. */
std::vector<unsigned char> EncodeWitness(const Witness& error, int n);

/** Decode a witness, rejecting a wrong length or a set padding bit. */
bool DecodeWitness(Span<const unsigned char> data, int n, Witness& error);

/**
 * Find a Pauli error of weight at most `w` whose syndrome is the instance's target, using
 * plain Prange information-set decoding. Good enough for regtest, not for real mining.
 * Returns false if nothing was found within `max_iterations`.
 */
bool Solve(const Instance& instance, int w, FastRandomContext& rng, int max_iterations,
           Witness& error);

/** What a block's nBits resolves to: weight bound, expected witnesses, hash target. */
struct DerivedWork {
    int w{0};
    uint32_t k{0};
    arith_uint256 target;
};

/**
 * Split a difficulty (2^256 / (target(nBits) + 1)) into a weight bound and a hash target:
 * w is the smallest weight with hardness[w] * DEQODE_K_MIN <= difficulty, K is
 * difficulty / hardness[w] and the target is (2^256 - 1) / K. Returns false if the
 * difficulty is below what the table can express.
 */
bool DeriveWork(const arith_uint256& difficulty,
                const Consensus::Params::DeqodeParams& params,
                DerivedWork& out);

/**
 * The difficulty a block's nBits encodes, or false if nBits is unusable or claims a
 * target above powLimit.
 */
bool DifficultyFromBits(uint32_t nBits, const Consensus::Params& consensus, arith_uint256& out);

/** Full verification of a header's task data against its deqode instance. */
bool CheckSolution(const CBlockHeader& header, const Consensus::Params& consensus,
                   const Consensus::Params::DeqodeParams& params);

} // namespace deqode

#endif // QUBITCOIN_POW_TASKS_DEQODE_H
