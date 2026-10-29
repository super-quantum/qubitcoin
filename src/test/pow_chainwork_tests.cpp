// Copyright (c) 2026 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <chain.h>
#include <consensus/params.h>
#include <pow.h>
#include <pow/schedule.h>
#include <pow/task_id.h>
#include <pow/tasks.h>
#include <primitives/block.h>
#include <versionbits.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

BOOST_AUTO_TEST_SUITE(pow_chainwork_tests)

namespace {

constexpr uint32_t ANCHOR_BITS{0x1e05ffff};
constexpr uint32_t TASK1_BITS{0x1d1fffff};

Consensus::Params MakeParams()
{
    Consensus::Params params{};
    params.nPowTargetSpacing = 600;
    params.nASERTHalfLife = 60 * 60;
    params.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    params.fPowNoRetargeting = false;
    params.fPowAllowMinDifficultyBlocks = false;

    params.powTasks[0] = Consensus::Params::PowTaskDefinition{
        .id = 0,
        .name = "qhash",
        .activation_height = 0,
        .deactivation_height = std::nullopt,
        .nInitialBits = ANCHOR_BITS,
    };
    return params;
}

void AddTask(Consensus::Params& params, uint8_t id, int from, uint32_t bits)
{
    params.powTasks[id] = Consensus::Params::PowTaskDefinition{
        .id = id,
        .name = "test",
        .activation_height = from,
        .deactivation_height = std::nullopt,
        .nInitialBits = bits,
    };
}

arith_uint256 Proof(uint32_t bits)
{
    CBlockIndex index;
    index.nBits = bits;
    return GetBlockProof(index);
}

//! floor(sqrt(x)) for x below 2^128, by binary search.
uint64_t ISqrt(const arith_uint256& x)
{
    uint64_t low{0};
    uint64_t high{uint64_t{1} << 63};
    while (high - low > 1) {
        const uint64_t mid = low + (high - low) / 2;
        if (arith_uint256{mid} * arith_uint256{mid} <= x) {
            low = mid;
        } else {
            high = mid;
        }
    }
    return low;
}

//! Block index entries without hashes, each with its scheduled nBits and credited as
//! AddToBlockIndex does.
struct FakeChain {
    std::vector<std::unique_ptr<CBlockIndex>> blocks;

    CBlockIndex* Mine(uint8_t task_id, int64_t time, const Consensus::Params& params, CBlockIndex* parent = nullptr)
    {
        auto index = std::make_unique<CBlockIndex>();
        index->pprev = parent ? parent : (blocks.empty() ? nullptr : blocks.back().get());
        index->nHeight = index->pprev ? index->pprev->nHeight + 1 : 0;
        index->nVersion = EncodeTaskId(VERSIONBITS_TOP_BITS, task_id);
        index->nTime = static_cast<uint32_t>(time);
        if (index->pprev) {
            CBlockHeader header;
            header.nVersion = index->nVersion;
            header.nTime = index->nTime;
            index->nBits = GetNextWorkRequired(index->pprev, &header, params);
        } else {
            index->nBits = params.powTasks.at(POW_DEFAULT_TASK_ID).nInitialBits;
        }
        index->BuildSkip();
        PopulateBlockTaskData(*index, params);
        index->nChainWork = (index->pprev ? index->pprev->nChainWork : 0) + GetBlockCredit(*index, params);
        blocks.push_back(std::move(index));
        return blocks.back().get();
    }
};

} // namespace

//! With one task the credit is the block's own proof, as before.
BOOST_AUTO_TEST_CASE(chainwork_single_task)
{
    for (const bool escape : {false, true}) {
        Consensus::Params params{MakeParams()};
        params.fPowAllowMinDifficultyBlocks = escape;
        FakeChain chain;
        int64_t time{1'700'000'000};
        chain.Mine(0, time, params);
        for (const int64_t gap : {300, 900, 60, 2000, 600, 600, 4000, 100, 1500, 700}) {
            time += gap;
            const CBlockIndex* block = chain.Mine(0, time, params);
            BOOST_CHECK(BlockCredit(*block) == GetBlockProof(*block));
        }
        const uint32_t limit{UintToArith256(params.powLimit).GetCompact()};
        BOOST_CHECK_EQUAL(escape, chain.blocks[7]->nBits == limit);
    }
}

BOOST_AUTO_TEST_CASE(chainwork_siblings_tie)
{
    Consensus::Params params{MakeParams()};
    AddTask(params, 1, /*from=*/0, TASK1_BITS);
    FakeChain chain;
    int64_t time{1'700'000'000};
    chain.Mine(0, time, params);
    for (int i = 1; i <= 8; ++i) chain.Mine(i % 2, time += 600, params);

    CBlockIndex* parent = chain.blocks.back().get();
    const CBlockIndex* zero = chain.Mine(0, time + 600, params, parent);
    const CBlockIndex* one = chain.Mine(1, time + 600, params, parent);
    BOOST_CHECK(zero->nBits != one->nBits);
    BOOST_CHECK(BlockCredit(*zero) == BlockCredit(*one));
    BOOST_CHECK(zero->nChainWork == one->nChainWork);
}

BOOST_AUTO_TEST_CASE(chainwork_geometric_mean)
{
    // Without retargeting each task stays at its anchor.
    Consensus::Params params{MakeParams()};
    params.fPowNoRetargeting = true;
    AddTask(params, 1, /*from=*/3, TASK1_BITS);
    params.powTasks[1].work_scale_den = 4;

    FakeChain chain;
    int64_t time{1'700'000'000};
    chain.Mine(0, time, params);
    const CBlockIndex* alone = chain.Mine(0, time += 600, params);
    chain.Mine(0, time += 600, params);
    const CBlockIndex* both = chain.Mine(1, time += 600, params);

    BOOST_CHECK(BlockCredit(*alone) == Proof(ANCHOR_BITS));
    const arith_uint256 expected{ISqrt(Proof(ANCHOR_BITS) * (Proof(TASK1_BITS) / 4))};
    BOOST_CHECK(BlockCredit(*both) == expected);
}

//! A fork that mines only one task gets less credit per unit of work it does.
BOOST_AUTO_TEST_CASE(chainwork_unmined_task)
{
    Consensus::Params params{MakeParams()};
    AddTask(params, 1, /*from=*/0, ANCHOR_BITS);

    FakeChain chain;
    int64_t time{1'700'000'000};
    chain.Mine(0, time, params);
    for (int i = 1; i <= 10; ++i) chain.Mine(i % 2, time += 600, params);
    CBlockIndex* fork = chain.blocks.back().get();

    struct Branch {
        arith_uint256 credit;
        arith_uint256 paid;
    };
    const auto grow = [&](bool alternate, int64_t gap) {
        CBlockIndex* tip = fork;
        arith_uint256 paid{0};
        int64_t t{time};
        for (int i = 1; i <= 30; ++i) {
            tip = chain.Mine(alternate ? i % 2 : 0, t += gap, params, tip);
            paid += GetBlockProof(*tip);
        }
        return Branch{tip->nChainWork - fork->nChainWork, paid};
    };

    const Branch mixed{grow(/*alternate=*/true, 600)};
    const Branch same_pace{grow(/*alternate=*/false, 600)};
    const Branch own_pace{grow(/*alternate=*/false, 1200)};

    const auto ratio = [](const Branch& b) { return (b.credit * 1000 / b.paid).GetLow64(); };
    BOOST_CHECK_GT(ratio(mixed), 950U);
    BOOST_CHECK_LT(ratio(same_pace), ratio(mixed) / 2);
    BOOST_CHECK_LT(ratio(own_pace), ratio(mixed) / 2);
    BOOST_CHECK(own_pace.credit < mixed.credit / 2);
}

//! A min-difficulty block is credited that task at its minimum difficulty.
BOOST_AUTO_TEST_CASE(chainwork_min_difficulty)
{
    Consensus::Params params{MakeParams()};
    params.fPowAllowMinDifficultyBlocks = true;
    AddTask(params, 1, /*from=*/0, TASK1_BITS);
    params.powTasks[1].work_scale_den = 4;

    FakeChain chain;
    int64_t time{1'700'000'000};
    chain.Mine(0, time, params);
    const CBlockIndex* last_one = chain.Mine(1, time += 600, params);
    for (int i = 0; i < 3; ++i) chain.Mine(0, time += 600, params);

    const PowSchedule schedule{PowSchedule::FromIndex(*chain.blocks.back(), params)};
    const int64_t edge{last_one->GetBlockTime() + 2 * 1200};
    const arith_uint256 inside{GetBlockCredit(schedule, edge, params)};
    const arith_uint256 escaped{GetBlockCredit(schedule, edge + 1, params)};
    BOOST_CHECK(escaped < inside);

    const uint32_t limit{arith_uint256{UintToArith256(params.powLimit) / 4}.GetCompact()};
    BOOST_CHECK_EQUAL(schedule.NextBits(1, edge + 1, params), limit);
    const TaskWork factors[]{{Proof(schedule.NextBits(0, edge + 1, params))}, {Proof(limit), 1, 4}};
    BOOST_CHECK(escaped == GeometricMeanWork(factors));
}

//! The credit depends only on the parent and the timestamp.
BOOST_AUTO_TEST_CASE(chainwork_ignores_own_task_and_bits)
{
    Consensus::Params params{MakeParams()};
    AddTask(params, 1, /*from=*/0, TASK1_BITS);
    FakeChain chain;
    int64_t time{1'700'000'000};
    chain.Mine(0, time, params);
    for (int i = 1; i <= 4; ++i) chain.Mine(i % 2, time += 600, params);
    CBlockIndex* parent = chain.blocks.back().get();
    const CBlockIndex* valid = chain.Mine(0, time + 600, params, parent);

    CBlockIndex junk;
    junk.pprev = parent;
    junk.nHeight = parent->nHeight + 1;
    junk.nVersion = EncodeTaskId(VERSIONBITS_TOP_BITS, 9);
    junk.nBits = 0x1b00ffff;
    junk.nTime = static_cast<uint32_t>(time + 600);
    junk.BuildSkip();
    PopulateBlockTaskData(junk, params);
    BOOST_CHECK(GetBlockCredit(junk, params) == BlockCredit(*valid));
}

BOOST_AUTO_TEST_CASE(geometric_mean_work)
{
    const auto mean = [](std::initializer_list<TaskWork> factors) {
        return GeometricMeanWork(std::span<const TaskWork>{factors.begin(), factors.size()});
    };

    BOOST_CHECK(GeometricMeanWork({}) == 0);

    const arith_uint256 big{(arith_uint256{1} << 200) + 12345};
    BOOST_CHECK(mean({{big}}) == big);
    BOOST_CHECK(mean({{big, 3, 2}}) == (big / 2) * 3);

    BOOST_CHECK(mean({{2}, {2}}) == 2);
    BOOST_CHECK(mean({{4}, {9}}) == 6);
    BOOST_CHECK(mean({{8}, {27}, {64}}) == 24);
    BOOST_CHECK(mean({{arith_uint256{1} << 200}, {arith_uint256{1} << 100}}) == arith_uint256{1} << 150);

    BOOST_CHECK(mean({{900000}, {23760000000, 1, 26400}}) == 900000);

    const arith_uint256 top{arith_uint256{1} << 255};
    BOOST_CHECK(mean({{top}, {top}, {top}, {top}}) == top);
    const arith_uint256 max{~arith_uint256{0}};
    const arith_uint256 all_max{mean({{max}, {max}, {max}, {max}})};
    BOOST_CHECK(all_max <= max);
    BOOST_CHECK(all_max > max - (max >> 60));

    const arith_uint256 p{arith_uint256{123456789} << 90};
    BOOST_CHECK(mean({{p}, {p}}) <= mean({{p}, {p + 1}}));
    BOOST_CHECK(mean({{p}, {0}}) == 0);
}

BOOST_AUTO_TEST_SUITE_END()
