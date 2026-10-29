// Copyright (c) 2026 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow/schedule.h>

#include <arith_uint256.h>
#include <chain.h>
#include <pow.h>
#include <pow/task_id.h>
#include <pow/tasks.h>
#include <primitives/block.h>

#include <cassert>

namespace {

//! Genesis for the default task, the activation block for every other.
int AnchorHeight(const Consensus::Params::PowTaskDefinition& def)
{
    return def.id == POW_DEFAULT_TASK_ID ? 0 : def.activation_height;
}

//! 2^256 / (target + 1), as GetBlockProof computes it.
arith_uint256 ProofForBits(uint32_t bits)
{
    bool negative;
    bool overflow;
    arith_uint256 target;
    target.SetCompact(bits, &negative, &overflow);
    if (negative || overflow || target == 0) return 0;
    return (~target / (target + 1)) + 1;
}

//! proof * num / den, multiplying first while that cannot overflow.
arith_uint256 Scaled(const TaskWork& factor)
{
    if (factor.scale_den == 0 || factor.scale_num == factor.scale_den) return factor.proof;
    if (factor.proof.bits() <= 224) {
        return (factor.proof * arith_uint256{factor.scale_num}) / arith_uint256{factor.scale_den};
    }
    return (factor.proof / arith_uint256{factor.scale_den}) * arith_uint256{factor.scale_num};
}

arith_uint256 Power(uint64_t base, size_t exponent)
{
    arith_uint256 result{1};
    for (size_t i = 0; i < exponent; ++i) result *= arith_uint256{base};
    return result;
}

} // namespace

PowSchedule PowSchedule::FromIndex(const CBlockIndex& prev, const Consensus::Params& params)
{
    PowSchedule schedule;
    schedule.m_parent_height = prev.nHeight;
    schedule.m_parent_time = prev.GetBlockTime();
    schedule.m_parent_bits = prev.nBits;

    for (const auto& [id, def] : params.powTasks) {
        const auto slot = TaskSlot(params, id);
        if (!slot) continue;
        TaskState& task = schedule.m_tasks[*slot];

        if (const CBlockIndex* last = FindPreviousTaskBlock(&prev, id, params)) {
            task.has_block = true;
            task.height = last->nHeight;
            task.time = last->GetBlockTime();
            task.spacing_sum = last->nTaskSpacingSum;
            task.bits = last->nBits;
        }

        // Not cached: a non-default task's anchor block depends on the chain.
        const int anchor_height = AnchorHeight(def);
        if (anchor_height <= prev.nHeight) {
            const CBlockIndex* anchor = prev.GetAncestor(anchor_height);
            assert(anchor);
            task.has_anchor = true;
            task.anchor_time = anchor->pprev ? anchor->pprev->GetBlockTime() : anchor->GetBlockTime();
        }
    }
    return schedule;
}

void PowSchedule::Advance(const CBlockHeader& header, const Consensus::Params& params)
{
    const int height = m_parent_height + 1;

    for (const auto& [id, def] : params.powTasks) {
        const auto slot = TaskSlot(params, id);
        if (!slot) continue;
        TaskState& task = m_tasks[*slot];
        if (!task.has_anchor && AnchorHeight(def) == height) {
            task.has_anchor = true;
            task.anchor_time = height == 0 ? header.GetBlockTime() : m_parent_time;
        }
    }

    // Same rules as PopulateBlockTaskData.
    const uint8_t task_id = DecodeTaskId(header.nVersion);
    const auto* def = LookupTaskDefinition(params, task_id);
    const auto slot = TaskSlot(params, task_id);
    if (def && slot && IsTaskActive(params, task_id, height)) {
        TaskState& task = m_tasks[*slot];
        task.spacing_sum = task.has_block
            ? task.spacing_sum + TargetSpacingPerTaskAtHeight(params, task.height + 1)
            : TargetSpacingPerTaskAtHeight(params, def->activation_height);
        task.has_block = true;
        task.height = height;
        task.time = header.GetBlockTime();
        task.bits = header.nBits;
    }

    m_parent_height = height;
    m_parent_time = header.GetBlockTime();
    m_parent_bits = header.nBits;
}

uint32_t PowSchedule::NextBits(uint8_t task_id, int64_t block_time, const Consensus::Params& params) const
{
    const auto* def = LookupTaskDefinition(params, task_id);
    const auto slot = TaskSlot(params, task_id);
    const TaskState* last = slot && m_tasks[*slot].has_block ? &m_tasks[*slot] : nullptr;

    // Special rule for regtest: we never retarget. Each task keeps its own target, since
    // the parent may be another task's block.
    if (params.fPowNoRetargeting) {
        if (last) return last->bits;
        return def ? def->nInitialBits : m_parent_bits;
    }

    assert(def);

    // No block solves this task yet: this one is the anchor.
    if (!last) return def->nInitialBits;

    const arith_uint256 pow_limit{TaskPowLimit(params, *def)};

    // Special difficulty rule for testnet: if no block of this task has been found for
    // twice its target spacing, allow a min-difficulty block.
    const int64_t block_spacing = TargetSpacingPerTaskAtHeight(params, last->height + 1);
    if (params.fPowAllowMinDifficultyBlocks && block_time > last->time + 2 * block_spacing) {
        return pow_limit.GetCompact();
    }

    assert(last->has_anchor);

    // The spacing is taken at the previous task block's height, so a task's difficulty
    // does not jump when other tasks activate or deactivate.
    const int64_t time_diff = m_parent_time - last->anchor_time;
    const int64_t ideal_time_diff = last->spacing_sum + block_spacing;
    return CalculateASERT(arith_uint256().SetCompact(def->nInitialBits),
                          time_diff,
                          ideal_time_diff,
                          pow_limit,
                          params.nASERTHalfLife).GetCompact();
}

bool PowSchedule::Expects(const CBlockHeader& header, const Consensus::Params& params) const
{
    const uint8_t task_id = DecodeTaskId(header.nVersion);
    if (!LookupTaskDefinition(params, task_id)) return false;
    if (!IsTaskActive(params, task_id, m_parent_height + 1)) return false;
    return header.nBits == NextBits(task_id, header.GetBlockTime(), params);
}

arith_uint256 GeometricMeanWork(std::span<const TaskWork> factors)
{
    assert(factors.size() <= Consensus::MAX_TRACKED_POW_TASKS);
    if (factors.empty()) return 0;
    if (factors.size() == 1) {
        const TaskWork& only = factors.front();
        if (only.scale_den == 0 || only.scale_num == only.scale_den) return only.proof;
        return (only.proof / arith_uint256{only.scale_den}) * arith_uint256{only.scale_num};
    }

    // Each value as m * 2^e with m < 2^62; four mantissas multiply to under 2^248.
    arith_uint256 product{1};
    unsigned int exponent{0};
    for (const TaskWork& factor : factors) {
        const arith_uint256 value{Scaled(factor)};
        if (value == 0) return 0;
        const unsigned int shift = value.bits() > 62 ? value.bits() - 62 : 0;
        product *= (value >> shift);
        exponent += shift;
    }
    const size_t n = factors.size();
    product <<= exponent % n;

    // The n-th root of something under 2^(62n + n - 1) is under 2^63.
    uint64_t low{0};
    uint64_t high{uint64_t{1} << 63};
    while (high - low > 1) {
        const uint64_t mid = low + (high - low) / 2;
        if (Power(mid, n) <= product) {
            low = mid;
        } else {
            high = mid;
        }
    }
    return arith_uint256{low} << (exponent / n);
}

arith_uint256 GetBlockCredit(const PowSchedule& schedule, int64_t block_time, const Consensus::Params& params)
{
    const int height = schedule.ParentHeight() + 1;
    std::array<TaskWork, Consensus::MAX_TRACKED_POW_TASKS> factors{};
    size_t count{0};
    for (const auto& [id, def] : params.powTasks) {
        if (count == factors.size()) break;
        if (!IsTaskActive(params, id, height)) continue;
        factors[count++] = TaskWork{
            .proof = ProofForBits(schedule.NextBits(id, block_time, params)),
            .scale_num = def.work_scale_num,
            .scale_den = def.work_scale_den,
        };
    }
    return GeometricMeanWork(std::span<const TaskWork>{factors.data(), count});
}

arith_uint256 GetBlockCredit(const CBlockIndex& block, const Consensus::Params& params)
{
    if (!block.pprev) return GetBlockProof(block);
    return GetBlockCredit(PowSchedule::FromIndex(*block.pprev, params), block.GetBlockTime(), params);
}
