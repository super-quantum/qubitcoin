// Copyright (c) 2026 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef QUBITCOIN_POW_SCHEDULE_H
#define QUBITCOIN_POW_SCHEDULE_H

#include <arith_uint256.h>
#include <consensus/params.h>

#include <array>
#include <cstdint>
#include <span>

class CBlockHeader;
class CBlockIndex;

/**
 * Per-task difficulty state as of one parent block: for each registered task, its most
 * recent block and its ASERT anchor.
 *
 * Built from the block index (FromIndex), or advanced one header at a time for headers
 * that are not in the index yet (headers presync, claimed-work checks).
 */
class PowSchedule
{
public:
    //! The schedule for a block whose parent is `prev`.
    static PowSchedule FromIndex(const CBlockIndex& prev, const Consensus::Params& params);

    //! Make `header`, which must build on the current parent, the new parent.
    void Advance(const CBlockHeader& header, const Consensus::Params& params);

    //! The nBits a block solving `task_id`, stamped `block_time`, needs on this parent.
    //! `task_id` must be registered unless retargeting is off.
    uint32_t NextBits(uint8_t task_id, int64_t block_time, const Consensus::Params& params) const;

    //! True if `header`'s task is active at its height and its nBits are exactly NextBits.
    bool Expects(const CBlockHeader& header, const Consensus::Params& params) const;

    //! Height of the parent; the block being judged is one above it.
    int ParentHeight() const { return m_parent_height; }

private:
    struct TaskState {
        //! The task's most recent block at or below the parent, if any.
        bool has_block{false};
        int height{-1};
        int64_t time{0};
        int64_t spacing_sum{0};
        uint32_t bits{0};
        //! Timestamp of the anchor block's parent (genesis: its own), once the anchor exists.
        bool has_anchor{false};
        int64_t anchor_time{0};
    };

    int m_parent_height{-1};
    int64_t m_parent_time{0};
    uint32_t m_parent_bits{0};
    //! Indexed by TaskSlot, like CBlockIndex::m_prev_task_height.
    std::array<TaskState, Consensus::MAX_TRACKED_POW_TASKS> m_tasks{};
};

/** One active task's factor in a block's chain-work credit. */
struct TaskWork {
    arith_uint256 proof;  //!< 2^256 / (target + 1) at the task's scheduled nBits
    uint32_t scale_num{1};
    uint32_t scale_den{1};
};

/**
 * Geometric mean of the factors' scaled work, rounded down. A single factor with scale 1/1
 * is returned unchanged. Integer-only: the factors are reduced to 62-bit mantissas and the
 * root is found by binary search. At most Consensus::MAX_TRACKED_POW_TASKS factors.
 */
arith_uint256 GeometricMeanWork(std::span<const TaskWork> factors);

/**
 * Chain work credited to a block stamped `block_time` on the schedule's parent: the
 * geometric mean, over the tasks active at its height, of each task's scaled work at its
 * scheduled nBits. It does not depend on which task the block solves, and a chain that
 * stops mining a task is credited that task's eased difficulty.
 */
arith_uint256 GetBlockCredit(const PowSchedule& schedule, int64_t block_time, const Consensus::Params& params);

/** The chain work `block` adds to its parent's. Genesis is credited its own proof. */
arith_uint256 GetBlockCredit(const CBlockIndex& block, const Consensus::Params& params);

#endif // QUBITCOIN_POW_SCHEDULE_H
