// Copyright (c) 2025 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <chain.h>
#include <consensus/params.h>
#include <pow/task_id.h>
#include <pow/tasks.h>
#include <primitives/block.h>
#include <versionbits.h>

#include <boost/test/unit_test.hpp>

#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

BOOST_AUTO_TEST_SUITE(pow_tasks_tests)

namespace {

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
        .nInitialBits = 0x1e05ffff,
    };
    return params;
}

//! Add a second task that turns on at `from` and (optionally) off at `until`.
void AddTask(Consensus::Params& params, uint8_t id, int from, std::optional<int> until)
{
    params.powTasks[id] = Consensus::Params::PowTaskDefinition{
        .id = id,
        .name = "test",
        .activation_height = from,
        .deactivation_height = until,
        .nInitialBits = 0x1e05ffff,
    };
}

//! A chain of block index entries without hashes.
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

    CBlockIndex* Tip() { return blocks.empty() ? nullptr : blocks.back().get(); }
};

} // namespace

BOOST_AUTO_TEST_CASE(task_lifecycle)
{
    Consensus::Params params{MakeParams()};
    AddTask(params, 1, /*from=*/100, /*until=*/200);

    BOOST_CHECK(LookupTaskDefinition(params, 0) != nullptr);
    BOOST_CHECK(LookupTaskDefinition(params, 7) == nullptr);
    BOOST_CHECK(!IsTaskActive(params, 7, 0));

    BOOST_CHECK(!IsTaskActive(params, 1, 99));
    BOOST_CHECK(IsTaskActive(params, 1, 100));
    BOOST_CHECK(IsTaskActive(params, 1, 199));
    BOOST_CHECK(!IsTaskActive(params, 1, 200));

    BOOST_CHECK_EQUAL(CountActiveTasks(params, 99), 1U);
    BOOST_CHECK_EQUAL(CountActiveTasks(params, 100), 2U);
    BOOST_CHECK_EQUAL(CountActiveTasks(params, 200), 1U);

    BOOST_CHECK_EQUAL(TargetSpacingPerTaskAtHeight(params, 99), 600);
    BOOST_CHECK_EQUAL(TargetSpacingPerTaskAtHeight(params, 100), 1200);
    BOOST_CHECK_EQUAL(TargetSpacingPerTaskAtHeight(params, 200), 600);

    // No active task at all: still one task's spacing.
    params.powTasks[0].deactivation_height = 10;
    BOOST_CHECK_EQUAL(CountActiveTasks(params, 10), 0U);
    BOOST_CHECK_EQUAL(TargetSpacingPerTaskAtHeight(params, 10), 600);
}

BOOST_AUTO_TEST_CASE(task_slots)
{
    Consensus::Params params{MakeParams()};
    AddTask(params, 5, /*from=*/0, /*until=*/std::nullopt);
    AddTask(params, 2, /*from=*/0, /*until=*/std::nullopt);

    BOOST_CHECK(TaskSlot(params, 0) == std::optional<size_t>{0});
    BOOST_CHECK(TaskSlot(params, 2) == std::optional<size_t>{1});
    BOOST_CHECK(TaskSlot(params, 5) == std::optional<size_t>{2});
    BOOST_CHECK(TaskSlot(params, 7) == std::nullopt);
}

BOOST_AUTO_TEST_CASE(previous_task_block)
{
    Consensus::Params params{MakeParams()};
    AddTask(params, 1, /*from=*/0, /*until=*/std::nullopt);
    AddTask(params, 3, /*from=*/0, /*until=*/std::nullopt);

    FakeChain chain;
    const CBlockIndex* zero_a = chain.Add(0, 1000, 0x1e05ffff, params);
    const CBlockIndex* one_a = chain.Add(1, 1600, 0x1e05ffff, params);
    chain.Add(0, 2200, 0x1e05ffff, params);
    const CBlockIndex* one_b = chain.Add(1, 2800, 0x1e05ffff, params);
    for (int i = 0; i < 500; ++i) {
        chain.Add(0, 3400 + i * 600, 0x1e05ffff, params);
    }

    BOOST_CHECK(FindPreviousTaskBlock(chain.Tip(), 1, params) == one_b);
    BOOST_CHECK(FindPreviousTaskBlock(one_b->pprev, 1, params) == one_a);
    BOOST_CHECK(FindPreviousTaskBlock(one_a->pprev, 0, params) == zero_a);
    BOOST_CHECK(FindPreviousTaskBlock(chain.Tip(), 3, params) == nullptr);
    BOOST_CHECK(FindPreviousTaskBlock(chain.Tip(), 9, params) == nullptr);
    BOOST_CHECK(FindPreviousTaskBlock(nullptr, 0, params) == nullptr);

    BOOST_CHECK_EQUAL(chain.Tip()->m_prev_task_height[0], chain.Tip()->nHeight);
    BOOST_CHECK_EQUAL(chain.Tip()->m_prev_task_height[1], 3);
    BOOST_CHECK_EQUAL(chain.Tip()->m_prev_task_height[2], -1);
}

BOOST_AUTO_TEST_CASE(task_spacing_sum)
{
    Consensus::Params params{MakeParams()};
    AddTask(params, 1, /*from=*/0, /*until=*/std::nullopt);

    // Two active tasks: 1200 s per task block.
    FakeChain chain;
    const CBlockIndex* zero_a = chain.Add(0, 1000, 0x1e05ffff, params);
    const CBlockIndex* one_a = chain.Add(1, 1600, 0x1e05ffff, params);
    const CBlockIndex* zero_b = chain.Add(0, 2200, 0x1e05ffff, params);
    const CBlockIndex* one_b = chain.Add(1, 2800, 0x1e05ffff, params);

    BOOST_CHECK_EQUAL(one_b->nPowTaskId, 1);
    BOOST_CHECK_EQUAL(zero_a->nTaskSpacingSum, 0);
    BOOST_CHECK_EQUAL(one_a->nTaskSpacingSum, 1200);
    BOOST_CHECK_EQUAL(zero_b->nTaskSpacingSum, 1200);
    BOOST_CHECK_EQUAL(one_b->nTaskSpacingSum, 2400);
}

BOOST_AUTO_TEST_CASE(solve_task_pow)
{
    const Consensus::Params params{MakeParams()};

    CBlockHeader header;
    header.nVersion = EncodeTaskId(VERSIONBITS_TOP_BITS, 0);
    header.nBits = 0x207fffff;
    header.nTime = 1723726478;
    header.nNonce = 0;
    BOOST_CHECK_EQUAL(SolveTaskProofOfWork(header, params), CheckTaskProofOfWork(header, params));
    BOOST_CHECK(header.vTaskData.empty());

    header.nVersion = EncodeTaskId(VERSIONBITS_TOP_BITS, 7);
    header.vTaskData = {0xaa, 0xbb};
    BOOST_CHECK(!SolveTaskProofOfWork(header, params));
    BOOST_CHECK_EQUAL(header.vTaskData.size(), 2U);
}

BOOST_AUTO_TEST_CASE(validate_pow_tasks)
{
    BOOST_CHECK_NO_THROW(ValidatePowTasks(MakeParams()));

    Consensus::Params two_tasks{MakeParams()};
    AddTask(two_tasks, 2, 10, std::nullopt);
    BOOST_CHECK_NO_THROW(ValidatePowTasks(two_tasks));

    Consensus::Params no_default{MakeParams()};
    no_default.powTasks.erase(POW_DEFAULT_TASK_ID);
    BOOST_CHECK_THROW(ValidatePowTasks(no_default), std::runtime_error);

    Consensus::Params mismatched_key{MakeParams()};
    mismatched_key.powTasks[3].id = 9;
    mismatched_key.powTasks[3].nInitialBits = 0x1e05ffff;
    BOOST_CHECK_THROW(ValidatePowTasks(mismatched_key), std::runtime_error);

    Consensus::Params no_anchor{MakeParams()};
    AddTask(no_anchor, 2, 10, std::nullopt);
    no_anchor.powTasks[2].nInitialBits = 0;
    BOOST_CHECK_THROW(ValidatePowTasks(no_anchor), std::runtime_error);

    Consensus::Params backwards{MakeParams()};
    AddTask(backwards, 2, 100, 50);
    BOOST_CHECK_THROW(ValidatePowTasks(backwards), std::runtime_error);

    Consensus::Params zero_scale{MakeParams()};
    AddTask(zero_scale, 2, 10, std::nullopt);
    zero_scale.powTasks[2].work_scale_den = 0;
    BOOST_CHECK_THROW(ValidatePowTasks(zero_scale), std::runtime_error);

    Consensus::Params weak_anchor{MakeParams()};
    AddTask(weak_anchor, 2, 10, std::nullopt);
    weak_anchor.powTasks[2].nInitialBits = UintToArith256(weak_anchor.powLimit).GetCompact();
    weak_anchor.powTasks[2].work_scale_den = 4;
    BOOST_CHECK_THROW(ValidatePowTasks(weak_anchor), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
