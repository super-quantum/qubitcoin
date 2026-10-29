// Copyright (c) 2025 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <chain.h>
#include <consensus/params.h>
#include <crypto/common.h>
#include <crypto/sha256.h>
#include <pow.h>
#include <pow/schedule.h>
#include <pow/task_id.h>
#include <pow/tasks.h>
#include <primitives/block.h>
#include <util/strencodings.h>
#include <versionbits.h>

#include <boost/test/unit_test.hpp>

#include <memory>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <vector>

BOOST_AUTO_TEST_SUITE(pow_asert_tests)

namespace {

constexpr uint32_t ANCHOR_BITS{0x1e05ffff};

Consensus::Params MakeParams()
{
    Consensus::Params params{};
    params.nPowTargetSpacing = 600;
    params.nASERTHalfLife = 2 * 24 * 60 * 60;
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

void AddTask(Consensus::Params& params, uint8_t id, int from)
{
    params.powTasks[id] = Consensus::Params::PowTaskDefinition{
        .id = id,
        .name = "test",
        .activation_height = from,
        .deactivation_height = std::nullopt,
        .nInitialBits = ANCHOR_BITS,
    };
}

struct FakeChain {
    std::vector<std::unique_ptr<CBlockIndex>> blocks;

    CBlockIndex* Add(uint8_t task_id, int64_t time, uint32_t bits, const Consensus::Params& params)
    {
        auto index = std::make_unique<CBlockIndex>();
        index->pprev = blocks.empty() ? nullptr : blocks.back().get();
        index->nHeight = index->pprev ? index->pprev->nHeight + 1 : 0;
        index->nVersion = EncodeTaskId(VERSIONBITS_TOP_BITS, task_id);
        index->nTime = static_cast<uint32_t>(time);
        index->nBits = bits;
        index->BuildSkip();
        PopulateBlockTaskData(*index, params);
        blocks.push_back(std::move(index));
        return blocks.back().get();
    }

    CBlockIndex* Tip() { return blocks.back().get(); }
};

CBlockHeader Candidate(uint8_t task_id, int64_t time)
{
    CBlockHeader header;
    header.nVersion = EncodeTaskId(VERSIONBITS_TOP_BITS, task_id);
    header.nTime = static_cast<uint32_t>(time);
    return header;
}

} // namespace

BOOST_AUTO_TEST_CASE(asert_first_block_gets_anchor_bits)
{
    Consensus::Params params{MakeParams()};
    AddTask(params, 1, /*from=*/0);

    FakeChain chain;
    chain.Add(0, 100000, ANCHOR_BITS, params);
    chain.Add(0, 100600, ANCHOR_BITS, params);

    const CBlockHeader candidate{Candidate(1, 101200)};
    BOOST_CHECK_EQUAL(GetNextWorkRequired(chain.Tip(), &candidate, params), ANCHOR_BITS);
}

BOOST_AUTO_TEST_CASE(asert_single_task_matches_legacy)
{
    // With only task 0 the schedule is plain ASERT anchored at genesis, with ideal time
    // nPowTargetSpacing * (height + 1).
    Consensus::Params params{MakeParams()};

    FakeChain chain;
    const int64_t start{1000000};
    for (int i = 0; i < 20; ++i) {
        chain.Add(0, start + i * 600, ANCHOR_BITS, params);
    }

    for (const int64_t drift : {-4800, -600, 0, 600, 4800}) {
        const CBlockIndex* prev = chain.Tip();
        const CBlockHeader candidate{Candidate(0, start + 20 * 600 + drift)};

        const int64_t nTimeDiff = prev->GetBlockTime() - chain.blocks.front()->GetBlockTime();
        const int64_t legacy_ideal = params.nPowTargetSpacing * (prev->nHeight + 1);
        const arith_uint256 legacy{CalculateASERT(
            arith_uint256().SetCompact(ANCHOR_BITS), nTimeDiff, legacy_ideal,
            UintToArith256(params.powLimit), params.nASERTHalfLife)};

        BOOST_CHECK_EQUAL(GetNextWorkRequired(prev, &candidate, params), legacy.GetCompact());
    }
}

BOOST_AUTO_TEST_CASE(asert_direction)
{
    Consensus::Params params{MakeParams()};

    FakeChain fast;
    FakeChain slow;
    const int64_t start{1000000};
    for (int i = 0; i < 20; ++i) {
        fast.Add(0, start + i * 300, ANCHOR_BITS, params);
        slow.Add(0, start + i * 1200, ANCHOR_BITS, params);
    }

    const CBlockHeader fast_candidate{Candidate(0, start + 20 * 300)};
    const CBlockHeader slow_candidate{Candidate(0, start + 20 * 1200)};

    arith_uint256 fast_target;
    arith_uint256 slow_target;
    arith_uint256 anchor_target;
    fast_target.SetCompact(GetNextWorkRequired(fast.Tip(), &fast_candidate, params));
    slow_target.SetCompact(GetNextWorkRequired(slow.Tip(), &slow_candidate, params));
    anchor_target.SetCompact(ANCHOR_BITS);

    BOOST_CHECK(fast_target < anchor_target); // harder
    BOOST_CHECK(slow_target > anchor_target); // easier
}

BOOST_AUTO_TEST_CASE(asert_task_activation)
{
    // Once a second task is active each task targets twice the spacing, so task 0's next
    // block after the activation is priced at 1200 s.
    const int64_t start{1000000};
    constexpr int kActivation{10};

    Consensus::Params solo{MakeParams()};
    FakeChain solo_chain;
    for (int i = 0; i < kActivation; ++i) solo_chain.Add(0, start + i * 600, ANCHOR_BITS, solo);
    const CBlockHeader solo_candidate{Candidate(0, start + kActivation * 600)};
    const uint32_t solo_next{GetNextWorkRequired(solo_chain.Tip(), &solo_candidate, solo)};

    Consensus::Params joined{MakeParams()};
    AddTask(joined, 1, /*from=*/kActivation);
    FakeChain joined_chain;
    for (int i = 0; i < kActivation; ++i) joined_chain.Add(0, start + i * 600, ANCHOR_BITS, joined);
    const CBlockHeader joined_candidate{Candidate(0, start + kActivation * 600)};
    const uint32_t joined_next{GetNextWorkRequired(joined_chain.Tip(), &joined_candidate, joined)};

    BOOST_CHECK_EQUAL(solo_chain.Tip()->nTaskSpacingSum, joined_chain.Tip()->nTaskSpacingSum);

    BOOST_CHECK_EQUAL(TargetSpacingPerTaskAtHeight(joined, kActivation), 1200);

    arith_uint256 solo_target;
    arith_uint256 joined_target;
    solo_target.SetCompact(solo_next);
    joined_target.SetCompact(joined_next);

    BOOST_CHECK(joined_target < solo_target);

    const int64_t nTimeDiff = joined_chain.Tip()->GetBlockTime() - joined_chain.blocks.front()->GetBlockTime();
    const arith_uint256 expected{CalculateASERT(
        arith_uint256().SetCompact(ANCHOR_BITS), nTimeDiff,
        joined_chain.Tip()->nTaskSpacingSum + 1200,
        UintToArith256(joined.powLimit), joined.nASERTHalfLife)};
    BOOST_CHECK_EQUAL(joined_next, expected.GetCompact());
}

BOOST_AUTO_TEST_CASE(asert_spacing_at_previous_task_block)
{
    // The spacing is taken where the task's previous block was, not at the tip.
    Consensus::Params params{MakeParams()};
    AddTask(params, 1, /*from=*/0);
    AddTask(params, 2, /*from=*/13);

    FakeChain chain;
    const int64_t start{1000000};
    for (int h = 0; h <= 10; ++h) chain.Add(0, start + h * 600, ANCHOR_BITS, params);
    for (int h = 11; h <= 13; ++h) chain.Add(1, start + h * 600, ANCHOR_BITS, params);

    const CBlockIndex* prev_task = FindPreviousTaskBlock(chain.Tip(), 0, params);
    BOOST_REQUIRE(prev_task != nullptr);
    BOOST_CHECK_EQUAL(prev_task->nHeight, 10);
    BOOST_CHECK_EQUAL(chain.Tip()->nHeight, 13);

    BOOST_CHECK_EQUAL(TargetSpacingPerTaskAtHeight(params, prev_task->nHeight + 1), 1200);
    BOOST_CHECK_EQUAL(TargetSpacingPerTaskAtHeight(params, chain.Tip()->nHeight + 1), 1800);

    const CBlockHeader candidate{Candidate(0, start + 14 * 600)};
    const int64_t nTimeDiff = chain.Tip()->GetBlockTime() - chain.blocks.front()->GetBlockTime();
    const arith_uint256 expected{CalculateASERT(
        arith_uint256().SetCompact(ANCHOR_BITS), nTimeDiff,
        prev_task->nTaskSpacingSum + 1200,
        UintToArith256(params.powLimit), params.nASERTHalfLife)};

    BOOST_CHECK_EQUAL(GetNextWorkRequired(chain.Tip(), &candidate, params), expected.GetCompact());
}

BOOST_AUTO_TEST_CASE(asert_per_task)
{
    Consensus::Params params{MakeParams()};
    AddTask(params, 1, /*from=*/0);

    FakeChain chain;
    const int64_t start{1000000};
    for (int i = 0; i < 10; ++i) {
        chain.Add(0, start + i * 1200, ANCHOR_BITS, params);
        chain.Add(1, start + i * 1200 + 100, ANCHOR_BITS, params);
    }

    const CBlockHeader zero_candidate{Candidate(0, start + 10 * 1200)};
    const CBlockHeader one_candidate{Candidate(1, start + 10 * 1200 + 100)};

    const uint32_t zero_next{GetNextWorkRequired(chain.Tip(), &zero_candidate, params)};
    const uint32_t one_next{GetNextWorkRequired(chain.Tip(), &one_candidate, params)};

    BOOST_CHECK(zero_next != one_next);
}

BOOST_AUTO_TEST_CASE(asert_anchor_per_chain)
{
    // Two chains with the same task-1 cadence relative to their own anchors: the
    // anchor time must come from each chain, not be shared between them.
    Consensus::Params params{MakeParams()};
    AddTask(params, 1, /*from=*/2);

    const int64_t start{1000000};

    FakeChain a;
    a.Add(0, start,        ANCHOR_BITS, params);  // height 0
    a.Add(0, start +  600, ANCHOR_BITS, params);  // height 1: the anchor's parent
    a.Add(1, start + 1200, ANCHOR_BITS, params);  // height 2: task 1 anchor
    a.Add(0, start + 1800, ANCHOR_BITS, params);  // height 3
    a.Add(1, start + 2400, ANCHOR_BITS, params);  // height 4

    // Same shape, shifted by 4400 s from height 1 onwards.
    FakeChain b;
    b.Add(0, start,        ANCHOR_BITS, params);  // height 0
    b.Add(0, start + 5000, ANCHOR_BITS, params);  // height 1: the anchor's parent
    b.Add(1, start + 5600, ANCHOR_BITS, params);  // height 2: task 1 anchor
    b.Add(0, start + 6200, ANCHOR_BITS, params);  // height 3
    b.Add(1, start + 6800, ANCHOR_BITS, params);  // height 4

    const CBlockHeader candidate_a{Candidate(1, start + 3000)};
    const CBlockHeader candidate_b{Candidate(1, start + 7400)};

    const uint32_t bits_a{GetNextWorkRequired(a.Tip(), &candidate_a, params)};
    const uint32_t bits_b{GetNextWorkRequired(b.Tip(), &candidate_b, params)};

    BOOST_CHECK_EQUAL(bits_a, bits_b);
}

BOOST_AUTO_TEST_CASE(min_difficulty_per_task)
{
    Consensus::Params params{MakeParams()};
    params.fPowAllowMinDifficultyBlocks = true;
    AddTask(params, 1, /*from=*/0);
    const uint32_t limit = UintToArith256(params.powLimit).GetCompact();

    // Each task targets 1200 s, so the threshold is 2400 s since that task's last block.
    FakeChain chain;
    chain.Add(0, 100000, ANCHOR_BITS, params);
    chain.Add(1, 100600, ANCHOR_BITS, params);
    const CBlockIndex* keeping_up = chain.Add(0, 101200, ANCHOR_BITS, params);
    chain.Add(0, 103000, ANCHOR_BITS, params);
    const CBlockIndex* stalled = chain.Add(0, 103500, ANCHOR_BITS, params);

    const CBlockHeader soon{Candidate(1, 101800)};
    BOOST_CHECK(GetNextWorkRequired(keeping_up, &soon, params) != limit);

    // Only 100 s after the tip, but 3000 s after the last task-1 block.
    const CBlockHeader late{Candidate(1, 103600)};
    BOOST_CHECK_EQUAL(GetNextWorkRequired(stalled, &late, params), limit);
}

BOOST_AUTO_TEST_CASE(task_pow_limit)
{
    // A task's easiest target is powLimit scaled by its work scale.
    Consensus::Params params{MakeParams()};
    params.nASERTHalfLife = 3600;
    AddTask(params, 1, /*from=*/0);
    params.powTasks[1].work_scale_den = 4;
    const uint32_t limit{UintToArith256(params.powLimit).GetCompact()};
    const uint32_t task_limit{arith_uint256{UintToArith256(params.powLimit) / 4}.GetCompact()};

    FakeChain chain;
    chain.Add(0, 100000, ANCHOR_BITS, params);
    chain.Add(1, 100600, ANCHOR_BITS, params);
    chain.Add(0, 100600 + 2 * 86400, ANCHOR_BITS, params);

    // Two days behind schedule, both tasks are eased as far as they go.
    const CBlockHeader zero{Candidate(0, 100600 + 2 * 86400 + 600)};
    const CBlockHeader one{Candidate(1, 100600 + 2 * 86400 + 600)};
    BOOST_CHECK_EQUAL(GetNextWorkRequired(chain.Tip(), &zero, params), limit);
    BOOST_CHECK_EQUAL(GetNextWorkRequired(chain.Tip(), &one, params), task_limit);

    params.fPowAllowMinDifficultyBlocks = true;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(chain.Tip(), &one, params), task_limit);
}

BOOST_AUTO_TEST_CASE(no_retargeting_per_task)
{
    constexpr uint32_t TASK1_BITS{0x1d00ffff};

    Consensus::Params params{MakeParams()};
    params.fPowNoRetargeting = true;
    AddTask(params, 1, /*from=*/0);
    params.powTasks[1].nInitialBits = TASK1_BITS;

    FakeChain chain;
    chain.Add(0, 100000, ANCHOR_BITS, params);

    const CBlockHeader first{Candidate(1, 100600)};
    BOOST_CHECK_EQUAL(GetNextWorkRequired(chain.Tip(), &first, params), TASK1_BITS);

    chain.Add(1, 100600, TASK1_BITS, params);
    chain.Add(0, 101200, ANCHOR_BITS, params);
    const CBlockHeader second{Candidate(1, 101800)};
    BOOST_CHECK_EQUAL(GetNextWorkRequired(chain.Tip(), &second, params), TASK1_BITS);

    const CBlockHeader zero{Candidate(0, 101800)};
    BOOST_CHECK_EQUAL(GetNextWorkRequired(chain.Tip(), &zero, params), ANCHOR_BITS);
}

namespace {

//! Task 1 from height 10, task 2 from 25 until 50, one-hour half-life.
Consensus::Params MixedParams(bool no_retargeting)
{
    Consensus::Params params{MakeParams()};
    params.nASERTHalfLife = 60 * 60;
    params.fPowAllowMinDifficultyBlocks = !no_retargeting;
    params.fPowNoRetargeting = no_retargeting;
    AddTask(params, 1, /*from=*/10);
    params.powTasks[1].nInitialBits = 0x1d1fffff;
    AddTask(params, 2, /*from=*/25);
    params.powTasks[2].deactivation_height = 50;
    params.powTasks[2].nInitialBits = 0x1c7fffff;
    return params;
}

//! A deterministic chain mixing the three tasks, with gaps long enough to trigger the
//! min-difficulty rule and task 1 dormant for twelve blocks.
void BuildMixedChain(FakeChain& chain, const Consensus::Params& params, int length)
{
    uint64_t state{0x5eed};
    const auto draw = [&state] {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state >> 33;
    };
    constexpr int64_t GAPS[]{60, 300, 600, 900, 1800, 4000};

    int64_t time{1'700'000'000};
    chain.Add(0, time, params.powTasks.at(0).nInitialBits, params);
    for (int height = 1; height < length; ++height) {
        std::vector<uint8_t> candidates;
        for (const auto& [id, def] : params.powTasks) {
            if (!IsTaskActive(params, id, height)) continue;
            if (id == 1 && height >= 30 && height < 42) continue;
            candidates.push_back(id);
        }
        const uint8_t task = candidates[draw() % candidates.size()];
        time += GAPS[draw() % std::size(GAPS)];
        const CBlockHeader header{Candidate(task, time)};
        chain.Add(task, time, GetNextWorkRequired(chain.Tip(), &header, params), params);
    }
}

std::vector<uint32_t> NextWorkTrace(const FakeChain& chain, const Consensus::Params& params)
{
    std::vector<uint32_t> trace;
    for (const auto& block : chain.blocks) {
        for (const auto& [id, def] : params.powTasks) {
            for (const int64_t delay : {600, 5000}) {
                const CBlockHeader header{Candidate(id, block->GetBlockTime() + delay)};
                trace.push_back(GetNextWorkRequired(block.get(), &header, params));
            }
        }
    }
    return trace;
}

std::string Digest(const std::vector<uint32_t>& values)
{
    CSHA256 hasher;
    for (const uint32_t value : values) {
        unsigned char bytes[4];
        WriteLE32(bytes, value);
        hasher.Write(bytes, sizeof(bytes));
    }
    unsigned char out[CSHA256::OUTPUT_SIZE];
    hasher.Finalize(out);
    return HexStr(out);
}

} // namespace

//! Pins every GetNextWorkRequired answer on the mixed chain.
BOOST_AUTO_TEST_CASE(next_work_mixed_chain)
{
    for (const bool no_retargeting : {false, true}) {
        const Consensus::Params params{MixedParams(no_retargeting)};
        FakeChain chain;
        BuildMixedChain(chain, params, /*length=*/80);
        const std::vector<uint32_t> trace{NextWorkTrace(chain, params)};
        BOOST_REQUIRE_EQUAL(trace.size(), 80U * 3 * 2);
        BOOST_CHECK_EQUAL(Digest(trace), no_retargeting
            ? "2ad01a5e0142e661ffe4e84ffe713c5942145e222c15dd3ca91c3bb64bc7eced"
            : "fdeb6532dd8e64c9f6e2ffed8959bf8f2cb5df8497d22eef98a8ff6fb4ef791a");

        const uint32_t limit{UintToArith256(params.powLimit).GetCompact()};
        const std::set<uint32_t> distinct(trace.begin(), trace.end());
        if (no_retargeting) {
            BOOST_CHECK_EQUAL(distinct.size(), 3U);
        } else {
            BOOST_CHECK(distinct.count(limit));
            BOOST_CHECK_GT(distinct.size(), 100U);
        }

        const CBlockHeader early{Candidate(1, chain.blocks[5]->GetBlockTime() + 600)};
        BOOST_CHECK_EQUAL(GetNextWorkRequired(chain.blocks[5].get(), &early, params), 0x1d1fffffU);
    }

    const Consensus::Params params{MixedParams(/*no_retargeting=*/false)};
    FakeChain chain;
    BuildMixedChain(chain, params, /*length=*/80);
    const CBlockHeader late{Candidate(0, chain.Tip()->GetBlockTime() + 5000)};
    BOOST_CHECK_EQUAL(GetNextWorkRequired(chain.Tip(), &late, params),
                      UintToArith256(params.powLimit).GetCompact());
}

BOOST_AUTO_TEST_CASE(schedule_advance_matches_index)
{
    for (const bool no_retargeting : {false, true}) {
        const Consensus::Params params{MixedParams(no_retargeting)};
        FakeChain chain;
        BuildMixedChain(chain, params, /*length=*/80);

        PowSchedule rolled{PowSchedule::FromIndex(*chain.blocks.front(), params)};
        for (size_t height = 1; height < chain.blocks.size(); ++height) {
            const CBlockIndex& block{*chain.blocks[height]};
            CBlockHeader header{Candidate(DecodeTaskId(block.nVersion), block.GetBlockTime())};
            header.nBits = block.nBits;
            BOOST_CHECK(rolled.Expects(header, params));

            rolled.Advance(header, params);
            const PowSchedule indexed{PowSchedule::FromIndex(block, params)};
            BOOST_REQUIRE_EQUAL(rolled.ParentHeight(), indexed.ParentHeight());
            for (const auto& [id, def] : params.powTasks) {
                for (const int64_t delay : {600, 5000}) {
                    const int64_t time{block.GetBlockTime() + delay};
                    BOOST_CHECK_EQUAL(rolled.NextBits(id, time, params), indexed.NextBits(id, time, params));
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(schedule_expects)
{
    const Consensus::Params params{MixedParams(/*no_retargeting=*/false)};
    FakeChain chain;
    BuildMixedChain(chain, params, /*length=*/80);
    const PowSchedule schedule{PowSchedule::FromIndex(*chain.Tip(), params)};
    const int64_t time{chain.Tip()->GetBlockTime() + 600};

    CBlockHeader good{Candidate(1, time)};
    good.nBits = schedule.NextBits(1, time, params);
    BOOST_CHECK(schedule.Expects(good, params));

    CBlockHeader wrong_bits{good};
    wrong_bits.nBits = good.nBits + 1;
    BOOST_CHECK(!schedule.Expects(wrong_bits, params));

    // Task 2 is inactive at height 80.
    CBlockHeader retired{Candidate(2, time)};
    retired.nBits = schedule.NextBits(2, time, params);
    BOOST_CHECK(!schedule.Expects(retired, params));

    CBlockHeader unknown{Candidate(7, time)};
    unknown.nBits = good.nBits;
    BOOST_CHECK(!schedule.Expects(unknown, params));
}

BOOST_AUTO_TEST_SUITE_END()
