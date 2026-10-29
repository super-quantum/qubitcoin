// Copyright (c) 2025 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <kernel/chainparams.h>
#include <pow/schedule.h>
#include <pow/task_id.h>
#include <pow/tasks.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <memory>
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

arith_uint256 Proof(uint32_t bits)
{
    CBlockIndex index;
    index.nBits = bits;
    return GetBlockProof(index);
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

BOOST_AUTO_TEST_CASE(single_task_networks)
{
    BOOST_CHECK_EQUAL(CChainParams::Main()->GetConsensus().powTasks.size(), 1U);
    BOOST_CHECK_EQUAL(CChainParams::SigNet({})->GetConsensus().powTasks.size(), 1U);
    BOOST_CHECK_EQUAL(CChainParams::RegTest({})->GetConsensus().powTasks.size(), 1U);
}

//! The anchors' geometric mean equals task 0's anchor work, so the credit is continuous
//! when task 1 activates.
BOOST_AUTO_TEST_CASE(deqode_work_scale)
{
    const auto check = [](const CChainParams& params, const std::string& network) {
        const auto& consensus = params.GetConsensus();
        const auto& zero = consensus.powTasks.at(POW_DEFAULT_TASK_ID);
        const auto& one = consensus.powTasks.at(POW_DEQODE_TASK_ID);
        const TaskWork anchors[]{
            {Proof(zero.nInitialBits)},
            {Proof(one.nInitialBits), one.work_scale_num, one.work_scale_den},
        };
        BOOST_CHECK_MESSAGE(GeometricMeanWork(anchors) == Proof(zero.nInitialBits), network);
    };
    CChainParams::RegTestOptions opts;
    opts.deqode_activation_height = 10;
    check(*CChainParams::RegTest(opts), "regtest");
}

BOOST_AUTO_TEST_SUITE_END()
