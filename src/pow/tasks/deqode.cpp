// Copyright (c) 2025 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow/tasks/deqode.h>

#include <crypto/common.h>
#include <crypto/sha256.h>
#include <primitives/block.h>
#include <random.h>
#include <streams.h>
#include <uint256.h>

#include <algorithm>
#include <bit>
#include <cassert>
#include <limits>
#include <string_view>
#include <utility>

namespace deqode {
namespace {

//! Number of 64-bit words needed to hold `bits` bits.
size_t WordsFor(const int bits)
{
    return static_cast<size_t>((bits + 63) / 64);
}

bool TestBit(const BitVector& bits, const int index)
{
    return (bits[static_cast<size_t>(index) / 64] >> (index % 64)) & 1;
}

void SetBit(BitVector& bits, const int index)
{
    bits[static_cast<size_t>(index) / 64] |= uint64_t{1} << (index % 64);
}

void XorInto(BitVector& target, const BitVector& source)
{
    for (size_t i = 0; i < target.size(); ++i) target[i] ^= source[i];
}

//! Domain tags for the code and syndrome streams.
constexpr std::string_view CODE_TAG{"QubitcoinDeqode/code"};
constexpr std::string_view SYNDROME_TAG{"QubitcoinDeqode/syndrome"};

/**
 * SHA-256 in counter mode, read as a stream of big-endian 64-bit words. Each block is
 * SHA256(SHA256(tag) || SHA256(tag) || seed || counter) with a big-endian 64-bit counter
 * starting at 1.
 */
class HashStream
{
public:
    HashStream(const std::string_view tag, const Span<const unsigned char> seed)
        : m_seed(seed.begin(), seed.end())
    {
        unsigned char tag_hash[CSHA256::OUTPUT_SIZE];
        CSHA256().Write(UCharCast(tag.data()), tag.size()).Finalize(tag_hash);
        std::copy(tag_hash, tag_hash + CSHA256::OUTPUT_SIZE, m_prefix);
        std::copy(tag_hash, tag_hash + CSHA256::OUTPUT_SIZE, m_prefix + CSHA256::OUTPUT_SIZE);
    }

    uint64_t Next()
    {
        if (m_used == 4) {
            ++m_counter;
            m_used = 0;
        }
        if (m_used == 0) {
            unsigned char counter[8];
            WriteBE64(counter, m_counter);
            CSHA256().Write(m_prefix, sizeof(m_prefix))
                     .Write(m_seed.data(), m_seed.size())
                     .Write(counter, sizeof(counter))
                     .Finalize(m_block);
        }
        const uint64_t word = ReadBE64(m_block + 8 * m_used);
        ++m_used;
        return word;
    }

private:
    unsigned char m_prefix[2 * CSHA256::OUTPUT_SIZE]{};
    const std::vector<unsigned char> m_seed;
    uint64_t m_counter{0};
    int m_used{4};
    unsigned char m_block[CSHA256::OUTPUT_SIZE]{};
};

/**
 * Bits off a HashStream, spent low end first. A draw wider than what is left discards
 * the remainder and starts a fresh word.
 */
class BitReservoir
{
public:
    BitReservoir(const std::string_view tag, const Span<const unsigned char> seed) : m_stream(tag, seed) {}

    uint64_t Take(const int width)
    {
        if (width <= 0) return 0;
        assert(width < 64);
        if (m_available < width) {
            m_reservoir = m_stream.Next();
            m_available = 64;
        }
        const uint64_t value = m_reservoir & ((uint64_t{1} << width) - 1);
        m_reservoir >>= width;
        m_available -= width;
        return value;
    }

    //! Uniform on [0, bound), by rejection sampling.
    uint64_t Below(const uint64_t bound)
    {
        int width = 0;
        while ((uint64_t{1} << width) < bound) ++width;
        while (true) {
            const uint64_t value = Take(width);
            if (value < bound) return value;
        }
    }

    //! One gate: 0 is H and 1 is S on `first`, 2 is CNOT from `first` to `second`.
    void NextGate(const int n, int& gate, int& first, int& second)
    {
        gate = static_cast<int>(Below(3));
        first = static_cast<int>(Below(static_cast<uint64_t>(n)));
        second = 0;
        if (gate == 2) {
            second = static_cast<int>(Below(static_cast<uint64_t>(n) - 1));
            if (second >= first) ++second;
        }
    }

private:
    HashStream m_stream;
    uint64_t m_reservoir{0};
    int m_available{0};
};

/** Index of the highest set bit, or -1 when the vector is zero. */
int HighestBit(const BitVector& bits)
{
    for (size_t i = bits.size(); i-- > 0;) {
        if (bits[i] != 0) return static_cast<int>(64 * i) + 63 - std::countl_zero(bits[i]);
    }
    return -1;
}

/**
 * Online Gaussian elimination over GF(2), keyed by each vector's highest set bit. Each
 * pivot remembers the Pauli that produced it, so Represent() yields a witness.
 */
class ColumnBasis
{
public:
    ColumnBasis(const int r, const int n)
        : m_used(static_cast<size_t>(r), false),
          m_vectors(static_cast<size_t>(r), BitVector(WordsFor(r), 0)),
          m_witnesses(static_cast<size_t>(r), Witness{BitVector(WordsFor(n), 0), BitVector(WordsFor(n), 0)}),
          m_rank(0) {}

    void Add(BitVector value, Witness witness)
    {
        while (true) {
            const int pivot = HighestBit(value);
            if (pivot < 0) return; // reduced to zero: linearly dependent, discard
            const size_t p = static_cast<size_t>(pivot);
            if (!m_used[p]) {
                m_used[p] = true;
                m_vectors[p] = std::move(value);
                m_witnesses[p] = std::move(witness);
                ++m_rank;
                return;
            }
            XorInto(value, m_vectors[p]);
            XorInto(witness.x, m_witnesses[p].x);
            XorInto(witness.z, m_witnesses[p].z);
        }
    }

    //! The witness for a target syndrome, if this basis spans it.
    bool Represent(BitVector target, Witness& out) const
    {
        out.x.assign(m_witnesses[0].x.size(), 0);
        out.z.assign(m_witnesses[0].z.size(), 0);
        while (true) {
            const int pivot = HighestBit(target);
            if (pivot < 0) return true;
            const size_t p = static_cast<size_t>(pivot);
            if (!m_used[p]) return false;
            XorInto(target, m_vectors[p]);
            XorInto(out.x, m_witnesses[p].x);
            XorInto(out.z, m_witnesses[p].z);
        }
    }

    int Rank() const { return m_rank; }

private:
    std::vector<bool> m_used;
    std::vector<BitVector> m_vectors;
    std::vector<Witness> m_witnesses;
    int m_rank;
};

} // namespace

bool ParamsAreSane(const Consensus::Params::DeqodeParams& params)
{
    if (params.n <= 1 || params.n > MAX_DEQODE_QUBITS) return false;
    if (params.r <= 0 || params.r > params.n) return false;
    if (params.gate_multiplier <= 0) return false;
    if (params.w_min < 0 || params.w_max < params.w_min || params.w_max > params.n) return false;
    if (params.hardness.size() != static_cast<size_t>(params.w_max - params.w_min + 1)) return false;
    // The table's ordering is checked once by ValidatePowTasks.
    return true;
}

std::vector<unsigned char> HeaderPrefix80(const CBlockHeader& header)
{
    // Not `ss << header`, which would append the task data.
    DataStream ss;
    ss << header.nVersion << header.hashPrevBlock << header.hashMerkleRoot
       << header.nTime << header.nBits << header.nNonce;
    return std::vector<unsigned char>(UCharCast(ss.data()), UCharCast(ss.data()) + ss.size());
}

std::vector<GateDraw> GateStream(const int n, const Span<const unsigned char> seed, const int count)
{
    BitReservoir reservoir(CODE_TAG, seed);
    std::vector<GateDraw> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        GateDraw draw{};
        reservoir.NextGate(n, draw.gate, draw.first, draw.second);
        out.push_back(draw);
    }
    return out;
}

void DeriveColumns(const int n, const int r, const Span<const unsigned char> seed,
                   const int gate_multiplier,
                   std::vector<BitVector>& col_x, std::vector<BitVector>& col_z)
{
    const size_t words = WordsFor(r);
    col_x.assign(static_cast<size_t>(n), BitVector(words, 0));
    col_z.assign(static_cast<size_t>(n), BitVector(words, 0));

    // Row i starts as Z on qubit i.
    for (int row = 0; row < r; ++row) SetBit(col_z[static_cast<size_t>(row)], row);

    BitReservoir reservoir(CODE_TAG, seed);
    const int64_t gates = int64_t{gate_multiplier} * n;
    for (int64_t i = 0; i < gates; ++i) {
        int gate, first, second;
        reservoir.NextGate(n, gate, first, second);
        const size_t f = static_cast<size_t>(first);
        switch (gate) {
        case 0: // H
            std::swap(col_x[f], col_z[f]);
            break;
        case 1: // S
            XorInto(col_z[f], col_x[f]);
            break;
        default: { // CNOT
            const size_t s = static_cast<size_t>(second);
            XorInto(col_x[s], col_x[f]);
            XorInto(col_z[f], col_z[s]);
            break;
        }
        }
    }
}

BitVector DeriveSyndrome(const int r, const Span<const unsigned char> seed)
{
    HashStream stream(SYNDROME_TAG, seed);
    BitVector out(WordsFor(r));
    for (auto& word : out) word = stream.Next();
    if (r % 64) out.back() &= (uint64_t{1} << (r % 64)) - 1;
    return out;
}

Instance DeriveInstance(const CBlockHeader& header, const Consensus::Params::DeqodeParams& params)
{
    assert(ParamsAreSane(params));

    Instance instance;
    instance.n = params.n;
    instance.r = params.r;

    // The code is seeded from the parent hash, so it is the same for every nonce.
    DeriveColumns(params.n, params.r, header.hashPrevBlock, params.gate_multiplier,
                  instance.col_x, instance.col_z);

    // The syndrome is seeded from the whole 80-byte header, nonce included.
    const std::vector<unsigned char> prefix{HeaderPrefix80(header)};
    unsigned char seed[CSHA256::OUTPUT_SIZE];
    CSHA256().Write(prefix.data(), prefix.size()).Finalize(seed);
    instance.syndrome = DeriveSyndrome(params.r, seed);

    return instance;
}

BitVector Syndrome(const Instance& instance, const Witness& error)
{
    BitVector out(WordsFor(instance.r), 0);
    for (int q = 0; q < instance.n; ++q) {
        if (TestBit(error.x, q)) XorInto(out, instance.col_z[static_cast<size_t>(q)]);
        if (TestBit(error.z, q)) XorInto(out, instance.col_x[static_cast<size_t>(q)]);
    }
    return out;
}

int PauliWeight(const Witness& error)
{
    int weight = 0;
    for (size_t i = 0; i < error.x.size(); ++i) {
        weight += std::popcount(error.x[i] | error.z[i]);
    }
    return weight;
}

std::vector<unsigned char> EncodeWitness(const Witness& error, const int n)
{
    const size_t mask_bytes = static_cast<size_t>((n + 7) / 8);
    std::vector<unsigned char> out(2 * mask_bytes, 0);
    for (int q = 0; q < n; ++q) {
        if (TestBit(error.x, q)) out[static_cast<size_t>(q) / 8] |= 1 << (q % 8);
        if (TestBit(error.z, q)) out[mask_bytes + static_cast<size_t>(q) / 8] |= 1 << (q % 8);
    }
    return out;
}

bool DecodeWitness(const Span<const unsigned char> data, const int n, Witness& error)
{
    const size_t mask_bytes = static_cast<size_t>((n + 7) / 8);
    if (data.size() != 2 * mask_bytes) return false;

    // Padding above qubit n-1 must be zero, so a solution has exactly one encoding.
    if (n % 8) {
        const unsigned char pad = static_cast<unsigned char>(0xFF << (n % 8));
        if ((data[mask_bytes - 1] & pad) || (data[2 * mask_bytes - 1] & pad)) return false;
    }

    const size_t words = WordsFor(n);
    error.x.assign(words, 0);
    error.z.assign(words, 0);
    for (int q = 0; q < n; ++q) {
        if (data[static_cast<size_t>(q) / 8] >> (q % 8) & 1) SetBit(error.x, q);
        if (data[mask_bytes + static_cast<size_t>(q) / 8] >> (q % 8) & 1) SetBit(error.z, q);
    }
    return true;
}

bool Solve(const Instance& instance, const int w, FastRandomContext& rng,
           const int max_iterations, Witness& error)
{
    // The 2n columns of the syndrome map: column q is an X on qubit q, column n + q a Z.
    std::vector<int> order(static_cast<size_t>(2 * instance.n));
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);

    for (int iteration = 0; iteration < max_iterations; ++iteration) {
        Shuffle(order.begin(), order.end(), rng);

        ColumnBasis basis(instance.r, instance.n);
        for (const int column : order) {
            const bool is_x = column < instance.n;
            const int q = is_x ? column : column - instance.n;

            Witness unit;
            unit.x.assign(WordsFor(instance.n), 0);
            unit.z.assign(WordsFor(instance.n), 0);
            SetBit(is_x ? unit.x : unit.z, q);

            basis.Add(is_x ? instance.col_z[static_cast<size_t>(q)] : instance.col_x[static_cast<size_t>(q)],
                      std::move(unit));
            if (basis.Rank() == instance.r) break; // the information set is complete
        }

        Witness candidate;
        if (!basis.Represent(instance.syndrome, candidate)) continue;
        if (PauliWeight(candidate) <= w) {
            error = std::move(candidate);
            return true;
        }
    }
    return false;
}

bool DifficultyFromBits(const uint32_t nBits, const Consensus::Params& consensus, arith_uint256& out)
{
    bool negative{false};
    bool overflow{false};
    arith_uint256 target;
    target.SetCompact(nBits, &negative, &overflow);
    if (negative || overflow || target == 0 || target > UintToArith256(consensus.powLimit)) {
        return false;
    }
    out = (~target / (target + 1)) + 1;
    return true;
}

bool DeriveWork(const arith_uint256& difficulty,
                const Consensus::Params::DeqodeParams& params,
                DerivedWork& out)
{
    if (params.hardness.empty()) return false;

    // The table is decreasing in w: take the first entry that leaves room for K_MIN.
    for (size_t i = 0; i < params.hardness.size(); ++i) {
        arith_uint256 cost;
        cost.SetCompact(params.hardness[i]);
        if (cost * DEQODE_K_MIN > difficulty) continue;

        out.w = params.w_min + static_cast<int>(i);
        const arith_uint256 k = difficulty / cost;
        out.k = k.bits() > 32 ? std::numeric_limits<uint32_t>::max() : static_cast<uint32_t>(k.GetLow64());
        out.target = (~arith_uint256{0}) / arith_uint256{out.k};
        return true;
    }

    return false;
}

bool CheckSolution(const CBlockHeader& header, const Consensus::Params& consensus,
                   const Consensus::Params::DeqodeParams& params)
{
    if (!ParamsAreSane(params)) return false;

    arith_uint256 difficulty;
    if (!DifficultyFromBits(header.nBits, consensus, difficulty)) return false;
    DerivedWork work;
    if (!DeriveWork(difficulty, params, work)) return false;

    // Cheapest checks first; the instance is derived only for a witness that passes the hash.
    Witness error;
    if (!DecodeWitness(header.vTaskData, params.n, error)) return false;

    // SHA256(prefix80 || witness), without the length byte the serialization adds.
    const std::vector<unsigned char> prefix{HeaderPrefix80(header)};
    uint256 acceptance;
    CSHA256().Write(prefix.data(), prefix.size())
             .Write(header.vTaskData.data(), header.vTaskData.size())
             .Finalize(acceptance.begin());
    if (UintToArith256(acceptance) > work.target) return false;

    if (PauliWeight(error) > work.w) return false;

    const Instance instance{DeriveInstance(header, params)};
    return Syndrome(instance, error) == instance.syndrome;
}

} // namespace deqode
