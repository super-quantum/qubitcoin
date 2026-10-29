// Copyright (c) 2025 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <crypto/sha256.h>
#include <hash.h>
#include <pow.h>
#include <pow/tasks.h>
#include <pow/tasks/deqode.h>
#include <primitives/block.h>
#include <random.h>
#include <streams.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <array>
#include <bit>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace deqode;

BOOST_AUTO_TEST_SUITE(deqode_tests)

namespace {

struct CodeVector {
    const char* note;
    int n;
    int r;
    int gate_multiplier;
    const char* seed_hex;
    size_t tableau_length_bytes;
    const char* tableau_sha256;
    const char* tableau_hex; // null for the larger cases
    std::array<std::array<int, 3>, 8> first_gates;
};

struct SyndromeVector {
    const char* note;
    int r;
    const char* seed_hex;
    const char* header_hex; // set only where the case composes the seed from a header
    std::vector<uint64_t> words;
    const char* syndrome_hex;
    int popcount;
};

//! Test vectors generated with an independent Python implementation.
const CodeVector CODE_VECTORS[]{
    {
        /*note=*/"required size; 8-byte seed",
        /*n=*/32, /*r=*/17, /*gate_multiplier=*/24,
        /*seed_hex=*/"0001020304050607",
        /*tableau_length_bytes=*/192,
        /*tableau_sha256=*/"d5b42690cbc52ba45f2fb30bd13cb11c8ff3d6da6d035e4ffa74e5efa3a26b8b",
        /*tableau_hex=*/"262f0141b801fb0f010e1501ff3001550601117d01f12b013d7200d2b800d09a01da5b01b03c0152b600b99201ac4a00a6f7011030008f51001d20016237013ee200284600ad4c01eddb01840700b00a00602700bdb7012e6701398301a549011d4a003bcd018a5e00122a01b13000a50500d21200119701596801a58e005009007b95001289003f0701d735019bc001ddc901ee3900487c00c5c400e56001096001bf9500013700705f01ad1700c52d01c46e01eadf01012800280f0117b601",
        /*first_gates=*/{{{0, 16, 0}, {1, 28, 0}, {1, 15, 0}, {2, 17, 20}, {0, 17, 0}, {0, 25, 0}, {0, 16, 0}, {2, 24, 14}}},
    },
    {
        /*note=*/"same size and seed as case 1 at a third the circuit depth",
        /*n=*/32, /*r=*/17, /*gate_multiplier=*/8,
        /*seed_hex=*/"0001020304050607",
        /*tableau_length_bytes=*/192,
        /*tableau_sha256=*/"743d73df8025892c196060235c940516b0579cd3602e2c75ab633045195a9fbc",
        /*tableau_hex=*/"40000001200002000000080084000050c60108200088e200587e00fc0100000000386201410100010000800000840000592201e42100804200e80201000000840c0086000008a800401000405001000101002001000000e400000a2000028000040200e46001c45000bc41019c2201840201840001180100000001000000000000000800000200002200400000a46101004000400000405001400000e42000e56001000001840000000c00780100002000c42200002001002101804200000000",
        /*first_gates=*/{{{0, 16, 0}, {1, 28, 0}, {1, 15, 0}, {2, 17, 20}, {0, 17, 0}, {0, 25, 0}, {0, 16, 0}, {2, 24, 14}}},
    },
    {
        /*note=*/"case 1 with one bit of the seed flipped (last byte 07 -> 87)",
        /*n=*/32, /*r=*/17, /*gate_multiplier=*/24,
        /*seed_hex=*/"0001020304050687",
        /*tableau_length_bytes=*/192,
        /*tableau_sha256=*/"10e358a2d77a3da0602f8d94b7c7d1c01e08407b8de73327dc49c21d6eea52dd",
        /*tableau_hex=*/"325501af4d01af6c01ad0900f5bc00f7ab002cd200aca601da30019a6c010425013d770192bb00c95301e2f7008faf0191f1006d9a01f7790003a0015b74005a4f01444001441801ab94014ae900c932008f7a0011ff00091a016d86013ba20190d101fbf80195770145fe0108d600a1cc01176401e71201383e015f9201db34015084019be20002da01bdf10121df01171f019a2a00b9ea0126dc001fb4018d8001434901ce4c01ffeb00f84f00cf530139950136db01c3af01c0b9012db200",
        /*first_gates=*/{{{0, 9, 0}, {0, 29, 0}, {0, 8, 0}, {2, 25, 15}, {1, 26, 0}, {1, 27, 0}, {0, 23, 0}, {0, 15, 0}}},
    },
    {
        /*note=*/"required size; 32-byte seed",
        /*n=*/96, /*r=*/59, /*gate_multiplier=*/24,
        /*seed_hex=*/"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
        /*tableau_length_bytes=*/1536,
        /*tableau_sha256=*/"4c54ac96d7c4efc673981c8487ec22c0e5adc37050fd01056d455d1adf16b5da",
        /*tableau_hex=*/nullptr,
        /*first_gates=*/{{{2, 89, 72}, {2, 29, 80}, {1, 44, 0}, {2, 6, 22}, {1, 16, 0}, {2, 47, 0}, {1, 80, 0}, {0, 85, 0}}},
    },
    {
        /*note=*/"required size again with a 1-byte seed, for SHA-256 padding",
        /*n=*/96, /*r=*/59, /*gate_multiplier=*/24,
        /*seed_hex=*/"5a",
        /*tableau_length_bytes=*/1536,
        /*tableau_sha256=*/"2ec170a4dac308ac96b1cdc6a30a823096ae7f1a6780ee7442dbd6b250652454",
        /*tableau_hex=*/nullptr,
        /*first_gates=*/{{{2, 14, 11}, {0, 15, 0}, {2, 32, 27}, {0, 65, 0}, {2, 45, 71}, {1, 43, 0}, {2, 47, 31}, {2, 83, 62}}},
    },
    {
        /*note=*/"r = 64: a whole number of column bytes and one 64-bit word",
        /*n=*/128, /*r=*/64, /*gate_multiplier=*/24,
        /*seed_hex=*/"ffeeddccbbaa998877665544332211000123456789abcdeffedcba9876543210",
        /*tableau_length_bytes=*/2048,
        /*tableau_sha256=*/"c98fcd457edcd9bc96e6a12b7ed6735cb3cf9421e81df0d4935e555be0ed107c",
        /*tableau_hex=*/nullptr,
        /*first_gates=*/{{{2, 92, 99}, {2, 16, 104}, {2, 43, 71}, {2, 0, 80}, {0, 0, 0}, {2, 30, 75}, {2, 59, 15}, {2, 4, 23}}},
    },
    {
        /*note=*/"r = 71: two 64-bit words, and 7 pad bits per column byte string",
        /*n=*/128, /*r=*/71, /*gate_multiplier=*/24,
        /*seed_hex=*/"ffeeddccbbaa998877665544332211000123456789abcdeffedcba9876543210",
        /*tableau_length_bytes=*/2304,
        /*tableau_sha256=*/"42f77adf95ca9d653ca2b364ac7a0d5cafe47edbdea91459e4da15c58fa310cf",
        /*tableau_hex=*/nullptr,
        /*first_gates=*/{{{2, 92, 99}, {2, 16, 104}, {2, 43, 71}, {2, 0, 80}, {0, 0, 0}, {2, 30, 75}, {2, 59, 15}, {2, 4, 23}}},
    },
};

const SyndromeVector SYNDROME_VECTORS[]{
    {
        /*note=*/"one word, 47 bits masked off the top; 8-byte seed",
        /*r=*/17,
        /*seed_hex=*/"1122334455667788",
        /*header_hex=*/nullptr,
        /*words=*/{0x0000000000015d27ULL},
        /*syndrome_hex=*/"275d01",
        /*popcount=*/10,
    },
    {
        /*note=*/"one word, 5 bits masked, 5 pad bits in the last byte",
        /*r=*/59,
        /*seed_hex=*/"e0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfee2",
        /*header_hex=*/nullptr,
        /*words=*/{0x07a08680dcf57e3cULL},
        /*syndrome_hex=*/"3c7ef5dc8086a007",
        /*popcount=*/30,
    },
    {
        /*note=*/"r = 64: a whole word and whole bytes, so nothing is masked",
        /*r=*/64,
        /*seed_hex=*/"e0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfee2",
        /*header_hex=*/nullptr,
        /*words=*/{0xa7a08680dcf57e3cULL},
        /*syndrome_hex=*/"3c7ef5dc8086a0a7",
        /*popcount=*/32,
    },
    {
        /*note=*/"r = 65: one bit into the second word, 7 pad bits, the ladder's top rank",
        /*r=*/65,
        /*seed_hex=*/"e0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfee2",
        /*header_hex=*/nullptr,
        /*words=*/{0xa7a08680dcf57e3cULL, 0x0000000000000001ULL},
        /*syndrome_hex=*/"3c7ef5dc8086a0a701",
        /*popcount=*/33,
    },
    {
        /*note=*/"r = 71: two words, 1 pad bit in the last byte",
        /*r=*/71,
        /*seed_hex=*/"e0e1e2e3e4e5e6e7e8e9eaebecedeeeff0f1f2f3f4f5f6f7f8f9fafbfcfdfee2",
        /*header_hex=*/nullptr,
        /*words=*/{0xa7a08680dcf57e3cULL, 0x0000000000000043ULL},
        /*syndrome_hex=*/"3c7ef5dc8086a0a743",
        /*popcount=*/35,
    },
    {
        /*note=*/"r = 300: five words, so the counter must reach a second SHA-256 block",
        /*r=*/300,
        /*seed_hex=*/"a5",
        /*header_hex=*/nullptr,
        /*words=*/{0x2b2605f51bfe5de7ULL, 0xbe674cbd04407f86ULL, 0x266a2fd471cf8c30ULL, 0x78907778015bf55dULL, 0x00000d9921c2fcf3ULL},
        /*syndrome_hex=*/"e75dfe1bf505262b867f4004bd4c67be308ccf71d42f6a265df55b0178779078f3fcc221990d",
        /*popcount=*/157,
    },
    {
        /*note=*/"composed from an 80-byte header: seed = SHA256(header), nonce = 0",
        /*r=*/65,
        /*seed_hex=*/"296fa41c51f262093665164390a656729ba9ce741afe46b34b5bb61a1ab8009a",
        /*header_hex=*/"20000000000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1fa1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebfc060c0dd681d00ffff00000000",
        /*words=*/{0x50651542f356ad29ULL, 0x0000000000000001ULL},
        /*syndrome_hex=*/"29ad56f34215655001",
        /*popcount=*/30,
    },
    {
        /*note=*/"the same header with nonce = 1",
        /*r=*/65,
        /*seed_hex=*/"c85d0818bf263caa168ef555f97fb31ead0accf93cbaddb82ffd8b7887a4c328",
        /*header_hex=*/"20000000000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1fa1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebfc060c0dd681d00ffff01000000",
        /*words=*/{0xa0c2d96bbcd3c654ULL, 0x0000000000000000ULL},
        /*syndrome_hex=*/"54c6d3bc6bd9c2a000",
        /*popcount=*/32,
    },
    {
        /*note=*/"code case 4's seed: the syndrome stream differs from the code stream",
        /*r=*/64,
        /*seed_hex=*/"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
        /*header_hex=*/nullptr,
        /*words=*/{0x01e63868fba0cdc0ULL},
        /*syndrome_hex=*/"c0cda0fb6838e601",
        /*popcount=*/28,
    },
};

//! Per qubit, ceil(r/8) bytes of X bits then ceil(r/8) bytes of Z bits, row i in bit i%8
//! of byte i/8.
std::vector<unsigned char> TableauBytes(const int n, const int r,
                                        const std::vector<BitVector>& col_x,
                                        const std::vector<BitVector>& col_z)
{
    const size_t width = static_cast<size_t>((r + 7) / 8);
    std::vector<unsigned char> out;
    out.reserve(2 * static_cast<size_t>(n) * width);
    for (int q = 0; q < n; ++q) {
        for (const auto* column : {&col_x[static_cast<size_t>(q)], &col_z[static_cast<size_t>(q)]}) {
            for (size_t byte = 0; byte < width; ++byte) {
                unsigned char value = 0;
                for (int bit = 0; bit < 8; ++bit) {
                    const size_t row = 8 * byte + static_cast<size_t>(bit);
                    if (row >= static_cast<size_t>(r)) break;
                    if (((*column)[row / 64] >> (row % 64)) & 1) value |= 1 << bit;
                }
                out.push_back(value);
            }
        }
    }
    return out;
}

//! ceil(r/8) bytes, bit i in bit i%8 of byte i/8.
std::vector<unsigned char> SyndromeBytes(const int r, const BitVector& syndrome)
{
    std::vector<unsigned char> out(static_cast<size_t>((r + 7) / 8), 0);
    for (int i = 0; i < r; ++i) {
        if ((syndrome[static_cast<size_t>(i) / 64] >> (i % 64)) & 1) {
            out[static_cast<size_t>(i) / 8] |= 1 << (i % 8);
        }
    }
    return out;
}

std::string Sha256Hex(const std::vector<unsigned char>& data)
{
    unsigned char digest[CSHA256::OUTPUT_SIZE];
    CSHA256().Write(data.data(), data.size()).Finalize(digest);
    return HexStr(digest);
}

CBlockHeader HeaderFromHex(const std::string& hex)
{
    DataStream ss{ParseHex(hex)};
    CBlockHeader header;
    ss >> header;
    return header;
}

Witness MakeWitness(const int n, const std::vector<std::pair<int, int>>& terms)
{
    Witness witness;
    witness.x.assign(static_cast<size_t>((n + 63) / 64), 0);
    witness.z.assign(static_cast<size_t>((n + 63) / 64), 0);
    for (const auto& [qubit, symbol] : terms) {
        if (symbol & 1) witness.x[static_cast<size_t>(qubit) / 64] |= uint64_t{1} << (qubit % 64);
        if (symbol & 2) witness.z[static_cast<size_t>(qubit) / 64] |= uint64_t{1} << (qubit % 64);
    }
    return witness;
}

//! A sizing with a single-entry hardness table, so the weight bound is always `w`.
Consensus::Params::DeqodeParams Sizing(const int n, const int r, const int w, const int gm = 24)
{
    Consensus::Params::DeqodeParams out;
    out.n = n;
    out.r = r;
    out.gate_multiplier = gm;
    out.w_min = w;
    out.w_max = w;
    out.hardness.push_back(arith_uint256{1}.GetCompact());
    return out;
}

} // namespace

BOOST_AUTO_TEST_CASE(gate_stream_vectors)
{
    for (const auto& vec : CODE_VECTORS) {
        BOOST_TEST_CONTEXT(vec.note) {
            const std::vector<unsigned char> seed{ParseHex(vec.seed_hex)};
            const std::vector<GateDraw> draws{GateStream(vec.n, seed, 8)};
            BOOST_REQUIRE_EQUAL(draws.size(), vec.first_gates.size());
            for (size_t i = 0; i < draws.size(); ++i) {
                BOOST_CHECK_EQUAL(draws[i].gate, vec.first_gates[i][0]);
                BOOST_CHECK_EQUAL(draws[i].first, vec.first_gates[i][1]);
                BOOST_CHECK_EQUAL(draws[i].second, vec.first_gates[i][2]);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(code_vectors)
{
    for (const auto& vec : CODE_VECTORS) {
        BOOST_TEST_CONTEXT(vec.note) {
            const std::vector<unsigned char> seed{ParseHex(vec.seed_hex)};
            std::vector<BitVector> col_x, col_z;
            DeriveColumns(vec.n, vec.r, seed, vec.gate_multiplier, col_x, col_z);

            const std::vector<unsigned char> tableau{TableauBytes(vec.n, vec.r, col_x, col_z)};
            BOOST_CHECK_EQUAL(tableau.size(), vec.tableau_length_bytes);
            BOOST_CHECK_EQUAL(Sha256Hex(tableau), vec.tableau_sha256);
            if (vec.tableau_hex != nullptr) BOOST_CHECK_EQUAL(HexStr(tableau), vec.tableau_hex);
        }
    }
}

BOOST_AUTO_TEST_CASE(syndrome_vectors)
{
    for (const auto& vec : SYNDROME_VECTORS) {
        BOOST_TEST_CONTEXT(vec.note) {
            const std::vector<unsigned char> seed{ParseHex(vec.seed_hex)};
            const BitVector syndrome{DeriveSyndrome(vec.r, seed)};

            BOOST_REQUIRE_EQUAL(syndrome.size(), vec.words.size());
            for (size_t i = 0; i < syndrome.size(); ++i) BOOST_CHECK_EQUAL(syndrome[i], vec.words[i]);
            BOOST_CHECK_EQUAL(HexStr(SyndromeBytes(vec.r, syndrome)), vec.syndrome_hex);

            int popcount = 0;
            for (const uint64_t word : syndrome) popcount += std::popcount(word);
            BOOST_CHECK_EQUAL(popcount, vec.popcount);

            if (vec.header_hex != nullptr) {
                // The syndrome seed is SHA256 of the 80-byte header.
                CBlockHeader header{HeaderFromHex(vec.header_hex)};
                BOOST_CHECK_EQUAL(HexStr(HeaderPrefix80(header)), vec.header_hex);
                BOOST_CHECK_EQUAL(Sha256Hex(HeaderPrefix80(header)), vec.seed_hex);
                header.vTaskData = {0xde, 0xad, 0xbe, 0xef};
                BOOST_CHECK_EQUAL(HexStr(HeaderPrefix80(header)), vec.header_hex);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(stabilizers_commute)
{
    const int n = 32, r = 17;
    const std::vector<unsigned char> seed{ParseHex("0001020304050607")};
    std::vector<BitVector> col_x, col_z;
    DeriveColumns(n, r, seed, 24, col_x, col_z);

    Instance instance;
    instance.n = n;
    instance.r = r;
    instance.col_x = col_x;
    instance.col_z = col_z;

    std::vector<Witness> rows(static_cast<size_t>(r));
    for (auto& row : rows) {
        row.x.assign(1, 0);
        row.z.assign(1, 0);
    }
    for (int q = 0; q < n; ++q) {
        for (int i = 0; i < r; ++i) {
            const uint64_t bit = uint64_t{1} << i;
            if (col_x[static_cast<size_t>(q)][0] & bit) rows[static_cast<size_t>(i)].x[0] |= uint64_t{1} << q;
            if (col_z[static_cast<size_t>(q)][0] & bit) rows[static_cast<size_t>(i)].z[0] |= uint64_t{1} << q;
        }
    }

    for (int i = 0; i < r; ++i) {
        const BitVector syndrome{Syndrome(instance, rows[static_cast<size_t>(i)])};
        BOOST_CHECK_EQUAL(syndrome[0], 0U);
        for (int j = 0; j < i; ++j) {
            const auto& a = rows[static_cast<size_t>(i)];
            const auto& b = rows[static_cast<size_t>(j)];
            BOOST_CHECK_EQUAL(std::popcount((a.x[0] & b.z[0]) ^ (a.z[0] & b.x[0])) % 2, 0);
        }
    }
}

BOOST_AUTO_TEST_CASE(syndrome_map)
{
    const int n = 8, r = 5;
    const std::vector<unsigned char> seed{ParseHex("0001020304050607")};
    Instance instance;
    instance.n = n;
    instance.r = r;
    DeriveColumns(n, r, seed, 24, instance.col_x, instance.col_z);

    const Witness a{MakeWitness(n, {{1, 1}, {4, 2}, {6, 3}})};
    const Witness b{MakeWitness(n, {{1, 2}, {2, 1}, {6, 1}})};
    Witness sum{MakeWitness(n, {})};
    sum.x[0] = a.x[0] ^ b.x[0];
    sum.z[0] = a.z[0] ^ b.z[0];
    BOOST_CHECK_EQUAL(Syndrome(instance, sum)[0], Syndrome(instance, a)[0] ^ Syndrome(instance, b)[0]);

    // Every syndrome is reached by the same number of errors.
    std::map<uint64_t, int> fibres;
    for (uint64_t x = 0; x < (uint64_t{1} << n); ++x) {
        for (uint64_t z = 0; z < (uint64_t{1} << n); ++z) {
            Witness error;
            error.x.assign(1, x);
            error.z.assign(1, z);
            ++fibres[Syndrome(instance, error)[0]];
        }
    }
    BOOST_CHECK_EQUAL(fibres.size(), size_t{1} << r);
    for (const auto& [syndrome, count] : fibres) {
        BOOST_CHECK_EQUAL(count, (1 << (2 * n)) >> r);
    }
}

BOOST_AUTO_TEST_CASE(pauli_weight)
{
    BOOST_CHECK_EQUAL(PauliWeight(MakeWitness(16, {})), 0);
    BOOST_CHECK_EQUAL(PauliWeight(MakeWitness(16, {{3, 1}})), 1);
    BOOST_CHECK_EQUAL(PauliWeight(MakeWitness(16, {{3, 2}})), 1);
    BOOST_CHECK_EQUAL(PauliWeight(MakeWitness(16, {{3, 3}})), 1);
    BOOST_CHECK_EQUAL(PauliWeight(MakeWitness(16, {{0, 1}, {1, 2}, {15, 3}})), 3);
    BOOST_CHECK_EQUAL(PauliWeight(MakeWitness(130, {{0, 3}, {64, 1}, {129, 2}})), 3);
}

BOOST_AUTO_TEST_CASE(witness_encoding)
{
    for (const int n : {8, 16, 17, 32, 71}) {
        BOOST_TEST_CONTEXT("n=" << n) {
            const Witness error{MakeWitness(n, {{0, 1}, {n - 1, 3}, {n / 2, 2}})};
            const std::vector<unsigned char> encoded{EncodeWitness(error, n)};
            BOOST_CHECK_EQUAL(encoded.size(), 2U * static_cast<size_t>((n + 7) / 8));

            Witness decoded;
            BOOST_CHECK(DecodeWitness(encoded, n, decoded));
            BOOST_CHECK(decoded.x == error.x);
            BOOST_CHECK(decoded.z == error.z);

            std::vector<unsigned char> truncated{encoded};
            truncated.pop_back();
            BOOST_CHECK(!DecodeWitness(truncated, n, decoded));
            std::vector<unsigned char> extended{encoded};
            extended.push_back(0);
            BOOST_CHECK(!DecodeWitness(extended, n, decoded));

            // Padding bits must be zero.
            if (n % 8) {
                const size_t mask_bytes = static_cast<size_t>((n + 7) / 8);
                const unsigned char pad = static_cast<unsigned char>(1 << (n % 8));
                std::vector<unsigned char> dirty_x{encoded};
                dirty_x[mask_bytes - 1] |= pad;
                BOOST_CHECK(!DecodeWitness(dirty_x, n, decoded));
                std::vector<unsigned char> dirty_z{encoded};
                dirty_z[2 * mask_bytes - 1] |= pad;
                BOOST_CHECK(!DecodeWitness(dirty_z, n, decoded));
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(params_are_sane)
{
    BOOST_CHECK(ParamsAreSane(Sizing(32, 17, 4)));
    BOOST_CHECK(!ParamsAreSane(Sizing(1, 1, 0)));
    BOOST_CHECK(!ParamsAreSane(Sizing(32, 33, 4)));
    BOOST_CHECK(!ParamsAreSane(Sizing(32, 0, 4)));
    BOOST_CHECK(!ParamsAreSane(Sizing(32, 17, 33)));
    BOOST_CHECK(!ParamsAreSane(Sizing(32, 17, 4, 0)));
    BOOST_CHECK(!ParamsAreSane(Sizing(MAX_DEQODE_QUBITS + 1, 17, 4)));
}

namespace {
CBlockHeader SolvableHeader(const uint32_t nonce)
{
    CBlockHeader header;
    header.nVersion = EncodeTaskId(0x20000000, 1);
    header.hashPrevBlock = uint256S("0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20");
    header.hashMerkleRoot = uint256S("2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40");
    header.nTime = 0x11223344;
    header.nBits = 0x203fffff; // difficulty 4: K = 4 with a single-entry table
    header.nNonce = nonce;
    return header;
}
} // namespace

BOOST_AUTO_TEST_CASE(solver)
{
    FastRandomContext rng{uint256S("000000000000000000000000000000000000000000000000000000000000002a")};

    for (const auto& sizing : {Sizing(32, 17, 4), Sizing(48, 28, 6)}) {
        BOOST_TEST_CONTEXT("n=" << sizing.n << " r=" << sizing.r << " w=" << sizing.w_max) {
            for (uint32_t nonce = 0; nonce < 40; ++nonce) {
                const Instance instance{DeriveInstance(SolvableHeader(nonce), sizing)};
                Witness error;
                BOOST_REQUIRE(Solve(instance, sizing.w_max, rng, 4000, error));
                BOOST_CHECK(Syndrome(instance, error) == instance.syndrome);
                BOOST_CHECK(PauliWeight(error) <= sizing.w_max);
            }
        }
    }

    // A solution for one nonce does not solve another.
    const auto sizing = Sizing(32, 17, 4);
    const Instance first{DeriveInstance(SolvableHeader(0), sizing)};
    const Instance second{DeriveInstance(SolvableHeader(1), sizing)};
    Witness error;
    BOOST_REQUIRE(Solve(first, sizing.w_max, rng, 4000, error));
    BOOST_CHECK(Syndrome(second, error) != second.syndrome);

    // Weight 0 cannot reach a non-zero syndrome.
    BOOST_CHECK(!Solve(first, 0, rng, 200, error));
}

BOOST_AUTO_TEST_CASE(solve_and_check_task_one)
{
    CChainParams::RegTestOptions opts;
    opts.deqode_activation_height = 0;
    const auto params{CChainParams::RegTest(opts)};
    const Consensus::Params& consensus{params->GetConsensus()};

    CBlockHeader header{SolvableHeader(0)};
    header.nBits = consensus.powTasks.at(POW_DEQODE_TASK_ID).nInitialBits;
    BOOST_CHECK(!CheckTaskProofOfWork(header, CChainParams::RegTest({})->GetConsensus()));

    bool mined = false;
    for (uint32_t nonce = 0; nonce < 256 && !mined; ++nonce) {
        header.nNonce = nonce;
        mined = SolveTaskProofOfWork(header, consensus);
    }
    BOOST_REQUIRE(mined);
    BOOST_CHECK_EQUAL(header.vTaskData.size(), 8U);
    BOOST_CHECK(CheckTaskProofOfWork(header, consensus));

    CBlockHeader tampered{header};
    tampered.vTaskData[0] ^= 1;
    BOOST_CHECK(!CheckTaskProofOfWork(tampered, consensus));

    CBlockHeader stripped{header};
    stripped.vTaskData.clear();
    BOOST_CHECK(!CheckTaskProofOfWork(stripped, consensus));
}

//! A header whose task-0 qhash would be the degenerate digest is still a valid deqode
//! block, with its own block hash.
BOOST_AUTO_TEST_CASE(degenerate_qhash_header)
{
    CChainParams::RegTestOptions opts;
    opts.deqode_activation_height = 0;
    const auto params{CChainParams::RegTest(opts)};
    const Consensus::Params& consensus{params->GetConsensus()};
    const auto& task{consensus.powTasks.at(POW_DEQODE_TASK_ID)};
    const auto& sizing{*task.deqode};

    arith_uint256 difficulty;
    BOOST_REQUIRE(DifficultyFromBits(task.nInitialBits, consensus, difficulty));
    DerivedWork work;
    BOOST_REQUIRE(DeriveWork(difficulty, sizing, work));

    const uint256 degenerate{uint256S("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff")};
    // After the last degenerate-output activation time.
    const uint32_t nTime{1800000000};

    int degenerate_seen = 0;
    int ordinary_seen = 0;
    for (uint32_t nonce = 0; nonce < 400 && (degenerate_seen == 0 || ordinary_seen == 0); ++nonce) {
        CBlockHeader header{SolvableHeader(nonce)};
        header.nBits = task.nInitialBits;
        header.nTime = nTime;

        const Instance instance{DeriveInstance(header, sizing)};
        Witness error;
        FastRandomContext rng{uint256S("0000000000000000000000000000000000000000000000000000000000000011")};
        if (!Solve(instance, work.w, rng, 4000, error)) continue;
        header.vTaskData = EncodeWitness(error, sizing.n);

        const std::vector<unsigned char> prefix{HeaderPrefix80(header)};
        uint256 acceptance;
        CSHA256().Write(prefix.data(), prefix.size())
                 .Write(header.vTaskData.data(), header.vTaskData.size())
                 .Finalize(acceptance.begin());
        if (UintToArith256(acceptance) > work.target) continue;

        if ((QHashWriter{header.nTime} << header).GetHash() == degenerate) {
            ++degenerate_seen;
        } else {
            ++ordinary_seen;
        }
        BOOST_CHECK(CheckSolution(header, consensus, sizing));
        BOOST_CHECK(header.GetHash() != degenerate);
    }
    BOOST_CHECK_GT(degenerate_seen, 0);
    BOOST_CHECK_GT(ordinary_seen, 0);
}

namespace {

//! A table dropping by a factor of 8 per weight step.
Consensus::Params::DeqodeParams SyntheticSizing(const int w_min = 2, const int w_max = 8)
{
    Consensus::Params::DeqodeParams s;
    s.n = 32;
    s.r = 17;
    s.gate_multiplier = 24;
    s.w_min = w_min;
    s.w_max = w_max;
    arith_uint256 cost = arith_uint256{1} << 40;
    for (int w = w_min; w <= w_max; ++w) {
        s.hardness.push_back(cost.GetCompact());
        cost >>= 3;
    }
    return s;
}

Consensus::Params ParamsWithDeqodeSizing(const Consensus::Params::DeqodeParams& sizing)
{
    Consensus::Params params{};
    params.nPowTargetSpacing = 600;
    params.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    params.powTasks[0] = Consensus::Params::PowTaskDefinition{
        .id = 0, .name = "qhash", .activation_height = 0,
        .deactivation_height = std::nullopt, .nInitialBits = 0x1e05ffff,
        .deqode = std::nullopt};
    params.powTasks[1] = Consensus::Params::PowTaskDefinition{
        .id = 1, .name = "deqode", .activation_height = 0,
        .deactivation_height = std::nullopt, .nInitialBits = 0x1e05ffff,
        .deqode = sizing};
    return params;
}

//! Difficulty 2^26 (w = 8, K = 16) and 2^36 (w = 4, K = 4) on SyntheticSizing's table.
constexpr uint32_t SYNTHETIC_EASY_BITS{0x1d3fffff};
constexpr uint32_t SYNTHETIC_HARD_BITS{0x1c0fffff};

} // namespace

BOOST_AUTO_TEST_CASE(hardness_table_validation)
{
    BOOST_CHECK_NO_THROW(ValidatePowTasks(ParamsWithDeqodeSizing(SyntheticSizing())));

    auto increasing = SyntheticSizing();
    std::swap(increasing.hardness[0], increasing.hardness[1]);
    BOOST_CHECK_THROW(ValidatePowTasks(ParamsWithDeqodeSizing(increasing)), std::runtime_error);

    // A step of 2^7 is wider than K_MAX / K_MIN.
    auto wide_step = SyntheticSizing(2, 3);
    const arith_uint256 cheap{arith_uint256{1} << 33};
    wide_step.hardness[1] = cheap.GetCompact();
    BOOST_CHECK_THROW(ValidatePowTasks(ParamsWithDeqodeSizing(wide_step)), std::runtime_error);

    auto short_table = SyntheticSizing();
    short_table.hardness.pop_back();
    BOOST_CHECK_THROW(ValidatePowTasks(ParamsWithDeqodeSizing(short_table)), std::runtime_error);

    auto too_many_qubits = SyntheticSizing();
    too_many_qubits.n = MAX_DEQODE_QUBITS + 1;
    BOOST_CHECK_THROW(ValidatePowTasks(ParamsWithDeqodeSizing(too_many_qubits)), std::runtime_error);

    Consensus::Params no_sizing{ParamsWithDeqodeSizing(SyntheticSizing())};
    no_sizing.powTasks[1].deqode = std::nullopt;
    BOOST_CHECK_THROW(ValidatePowTasks(no_sizing), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(derive_work)
{
    const auto sizing = SyntheticSizing();

    arith_uint256 easiest, hardest;
    easiest.SetCompact(sizing.hardness.back());
    hardest.SetCompact(sizing.hardness.front());

    int prev_w{sizing.w_max};
    for (arith_uint256 d = easiest * DEQODE_K_MIN; d <= hardest * DEQODE_K_MAX; d *= 2) {
        DerivedWork work;
        BOOST_REQUIRE(DeriveWork(d, sizing, work));
        BOOST_CHECK(work.w >= sizing.w_min && work.w <= prev_w);
        BOOST_CHECK(work.k >= DEQODE_K_MIN && work.k <= DEQODE_K_MAX);
        prev_w = work.w;
    }

    // At w_min, a higher difficulty only tightens the target.
    DerivedWork prev;
    arith_uint256 d{hardest * DEQODE_K_MIN};
    BOOST_REQUIRE(DeriveWork(d, sizing, prev));
    for (int i = 0; i < 40; ++i) {
        d *= 2;
        DerivedWork next;
        if (!DeriveWork(d, sizing, next)) break;
        BOOST_CHECK_EQUAL(next.w, sizing.w_min);
        BOOST_CHECK(next.target <= prev.target);
        prev = next;
    }

    DerivedWork work;
    BOOST_CHECK(!DeriveWork(easiest / 2, sizing, work));

    const Consensus::Params consensus{ParamsWithDeqodeSizing(sizing)};
    arith_uint256 difficulty;
    BOOST_CHECK(!DifficultyFromBits(0x20ffffff, consensus, difficulty));
    BOOST_CHECK(!DifficultyFromBits(0x00000000, consensus, difficulty));
    BOOST_CHECK(DifficultyFromBits(SYNTHETIC_EASY_BITS, consensus, difficulty));
}

//! A witness is accepted exactly when its weight is within the derived bound and its
//! acceptance hash is below the derived target.
BOOST_AUTO_TEST_CASE(acceptance_rule)
{
    const auto sizing = SyntheticSizing();
    const Consensus::Params consensus{ParamsWithDeqodeSizing(sizing)};

    arith_uint256 difficulty;
    BOOST_REQUIRE(DifficultyFromBits(SYNTHETIC_HARD_BITS, consensus, difficulty));
    DerivedWork work;
    BOOST_REQUIRE(DeriveWork(difficulty, sizing, work));
    BOOST_REQUIRE_EQUAL(work.w, 4);
    BOOST_REQUIRE_EQUAL(work.k, DEQODE_K_MIN);

    FastRandomContext rng{uint256S("0000000000000000000000000000000000000000000000000000000000000017")};
    int accepted = 0;
    int refused_on_weight = 0;
    int refused_on_hash = 0;
    for (uint32_t nonce = 0; nonce < 512; ++nonce) {
        CBlockHeader header{SolvableHeader(nonce)};
        header.nBits = SYNTHETIC_HARD_BITS;

        // Solve at the loosest bound so some witnesses exceed the derived one.
        const Instance instance{DeriveInstance(header, sizing)};
        Witness error;
        if (!Solve(instance, sizing.w_max, rng, 4000, error)) continue;
        header.vTaskData = EncodeWitness(error, sizing.n);

        const std::vector<unsigned char> prefix{HeaderPrefix80(header)};
        uint256 acceptance;
        CSHA256().Write(prefix.data(), prefix.size())
                 .Write(header.vTaskData.data(), header.vTaskData.size())
                 .Finalize(acceptance.begin());

        const bool light{PauliWeight(error) <= work.w};
        const bool lucky{UintToArith256(acceptance) <= work.target};
        BOOST_CHECK_EQUAL(CheckSolution(header, consensus, sizing), light && lucky);

        if (light && lucky) ++accepted;
        else if (!light) ++refused_on_weight;
        else ++refused_on_hash;
    }
    BOOST_CHECK_GT(accepted, 0);
    BOOST_CHECK_GT(refused_on_weight, 0);
    BOOST_CHECK_GT(refused_on_hash, 0);
}

BOOST_AUTO_TEST_CASE(solve_at_derived_weight)
{
    const auto sizing = SyntheticSizing();
    const Consensus::Params consensus{ParamsWithDeqodeSizing(sizing)};

    arith_uint256 difficulty;
    BOOST_REQUIRE(DifficultyFromBits(SYNTHETIC_HARD_BITS, consensus, difficulty));
    DerivedWork work;
    BOOST_REQUIRE(DeriveWork(difficulty, sizing, work));

    for (const uint32_t bits : {SYNTHETIC_EASY_BITS, SYNTHETIC_HARD_BITS}) {
        CBlockHeader header{SolvableHeader(0)};
        header.nBits = bits;
        bool mined = false;
        for (uint32_t nonce = 0; nonce < 4096 && !mined; ++nonce) {
            header.nNonce = nonce;
            mined = SolveTaskProofOfWork(header, consensus);
        }
        BOOST_REQUIRE(mined);

        if (bits == SYNTHETIC_EASY_BITS) {
            // The same block is not valid at a higher difficulty.
            CBlockHeader harder{header};
            harder.nBits = SYNTHETIC_HARD_BITS;
            BOOST_CHECK(!CheckTaskProofOfWork(harder, consensus));
        } else {
            Witness error;
            BOOST_REQUIRE(DecodeWitness(header.vTaskData, sizing.n, error));
            BOOST_CHECK(PauliWeight(error) <= work.w);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
