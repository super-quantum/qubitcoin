// Copyright (c) 2025 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <consensus/amount.h>
#include <consensus/merkle.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

// Mines a genesis block for a given target and prints the lines kernel/chainparams.cpp needs.

//! Same as CreateGenesisBlock in kernel/chainparams.cpp.
static CBlock BuildGenesis(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion,
                           const CAmount& reward)
{
    const char* pszTimestamp = "superquantum";
    const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;

    CMutableTransaction txNew;
    txNew.version = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4)
                                       << std::vector<unsigned char>((const unsigned char*)pszTimestamp,
                                                                     (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = reward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime = nTime;
    genesis.nBits = nBits;
    genesis.nNonce = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

static void Usage()
{
    std::fprintf(stderr,
        "usage: qubitcoin-genesis --bits=0xHEX [options]\n"
        "\n"
        "  --bits=0xHEX      compact target to mine against (required)\n"
        "  --time=N          starting unix timestamp (default: now)\n"
        "  --nonce=N         starting nonce (default: 0)\n"
        "  --version=N       header nVersion (default: 1)\n"
        "  --reward=N        genesis output in whole coins (default: 50)\n"
        "\n"
        "Every nonce is a cuStateVec simulation, so this needs a GPU with free memory\n"
        "and runs at roughly the node's own hash rate. Expect 2^k nonces for a target\n"
        "of work 2^k.\n");
}

static bool ArgValue(const char* arg, const char* name, std::string& out)
{
    const size_t len = strlen(name);
    if (strncmp(arg, name, len) != 0) return false;
    out = arg + len;
    return true;
}

int main(int argc, char* argv[])
{
    uint32_t nBits{0};
    uint32_t nTime{static_cast<uint32_t>(std::time(nullptr))};
    uint32_t nNonce{0};
    int32_t nVersion{1};
    CAmount reward{50 * COIN};

    for (int i = 1; i < argc; ++i) {
        std::string value;
        if (ArgValue(argv[i], "--bits=", value)) {
            nBits = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 0));
        } else if (ArgValue(argv[i], "--time=", value)) {
            nTime = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
        } else if (ArgValue(argv[i], "--nonce=", value)) {
            nNonce = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
        } else if (ArgValue(argv[i], "--version=", value)) {
            nVersion = static_cast<int32_t>(std::strtol(value.c_str(), nullptr, 0));
        } else if (ArgValue(argv[i], "--reward=", value)) {
            reward = static_cast<CAmount>(std::strtoll(value.c_str(), nullptr, 10)) * COIN;
        } else {
            Usage();
            return 1;
        }
    }

    if (nBits == 0) {
        Usage();
        return 1;
    }

    bool negative{false};
    bool overflow{false};
    arith_uint256 target;
    target.SetCompact(nBits, &negative, &overflow);
    if (negative || overflow || target == 0) {
        std::fprintf(stderr, "qubitcoin-genesis: nBits 0x%08x does not decode to a usable target\n", nBits);
        return 1;
    }

    std::fprintf(stderr, "target  0x%s\n", target.GetHex().c_str());
    std::fprintf(stderr, "mining  nBits=0x%08x version=%d time=%u ...\n", nBits, nVersion, nTime);

    CBlock genesis = BuildGenesis(nTime, nNonce, nBits, nVersion, reward);
    const uint256 merkle = genesis.hashMerkleRoot;

    uint64_t tried{0};
    while (UintToArith256(genesis.GetHash()) > target) {
        ++genesis.nNonce;
        ++tried;
        if (genesis.nNonce == 0) {
            ++genesis.nTime;
            std::fprintf(stderr, "nonce space exhausted, advancing to time=%u\n", genesis.nTime);
        }
        if ((tried % 10000) == 0) {
            std::fprintf(stderr, "  %llu nonces\n", static_cast<unsigned long long>(tried));
        }
    }

    std::fprintf(stderr, "found after %llu nonces\n", static_cast<unsigned long long>(tried));
    std::printf("genesis = CreateGenesisBlock(%u, %u, 0x%08x, %d, %lld * COIN);\n",
                genesis.nTime, genesis.nNonce, nBits, nVersion,
                static_cast<long long>(reward / COIN));
    std::printf("consensus.hashGenesisBlock = uint256S(\"0x%s\");\n",
                genesis.GetHash().GetHex().c_str());
    std::printf("assert(genesis.hashMerkleRoot == uint256S(\"0x%s\"));\n", merkle.GetHex().c_str());
    return 0;
}
