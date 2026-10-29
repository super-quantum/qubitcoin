// Copyright (c) 2025 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <kernel/chainparams.h>
#include <pow/tasks.h>

#include <boost/test/unit_test.hpp>

#include <string>

BOOST_AUTO_TEST_SUITE(chainparams_tests)

namespace {

void CheckGenesisIsMined(const CChainParams& params, const std::string& name)
{
    const CBlock& genesis = params.GenesisBlock();
    BOOST_CHECK_MESSAGE(CheckTaskProofOfWork(genesis, params.GetConsensus()),
                        name + ": genesis " + genesis.GetHash().GetHex() +
                            " does not satisfy its own nBits");
}

} // namespace

BOOST_AUTO_TEST_CASE(genesis_is_mined)
{
    CheckGenesisIsMined(*CChainParams::Main(), "main");
    CheckGenesisIsMined(*CChainParams::RegTest({}), "regtest");
    // Testnet and signet still ship Bitcoin's genesis, which does not meet their targets.
}

BOOST_AUTO_TEST_CASE(genesis_hash_constants)
{
    const auto check = [](const CChainParams& params, const std::string& name) {
        BOOST_CHECK_MESSAGE(params.GenesisBlock().GetHash() == params.GetConsensus().hashGenesisBlock,
                            name + ": hashGenesisBlock does not match the genesis block");
    };
    check(*CChainParams::Main(), "main");
    check(*CChainParams::TestNet(), "test");
    check(*CChainParams::SigNet({}), "signet");
    check(*CChainParams::RegTest({}), "regtest");
}

BOOST_AUTO_TEST_SUITE_END()
