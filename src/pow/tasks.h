// Copyright (c) 2025 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef QUBITCOIN_POW_TASKS_H
#define QUBITCOIN_POW_TASKS_H

#include <arith_uint256.h>
#include <consensus/params.h>
#include <pow/task_id.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

class CBlockHeader;
class CBlockIndex;
class BlockValidationState;

/** Look up the consensus definition for a given task id. */
const Consensus::Params::PowTaskDefinition* LookupTaskDefinition(const Consensus::Params& params, uint8_t task_id);

/** Return true if the specified task may be used at the provided height. */
bool IsTaskActive(const Consensus::Params& params, uint8_t task_id, int height);

/** Count the number of tasks that are active at the provided height. */
size_t CountActiveTasks(const Consensus::Params& params, int height);

/** Return the current per-task target spacing (in seconds) for a block at the provided height. */
int64_t TargetSpacingPerTaskAtHeight(const Consensus::Params& params, int height);

/** The easiest target a task may reach: powLimit scaled by its work scale, capped at powLimit. */
inline arith_uint256 TaskPowLimit(const Consensus::Params& params, const Consensus::Params::PowTaskDefinition& task)
{
    const arith_uint256 limit{UintToArith256(params.powLimit)};
    if (task.work_scale_num >= task.work_scale_den) return limit;
    return limit / task.work_scale_den * task.work_scale_num;
}

/**
 * Check that a network's task table is well formed. Throws std::runtime_error if not.
 * Inline because kernel/chainparams.cpp is also built into libbitcoin_common, which does
 * not link pow/tasks.cpp.
 */
inline void ValidatePowTasks(const Consensus::Params& params)
{
    const auto Fail = [](const std::string& why) {
        throw std::runtime_error("Invalid proof-of-work task table: " + why);
    };

    if (params.powTasks.size() > Consensus::MAX_TRACKED_POW_TASKS) Fail("too many tasks");

    const auto default_task = params.powTasks.find(POW_DEFAULT_TASK_ID);
    if (default_task == params.powTasks.end()) Fail("task 0 is not registered");
    if (default_task->second.activation_height != 0) Fail("task 0 does not activate at height 0");
    if (default_task->second.deactivation_height) Fail("task 0 deactivates");

    for (const auto& [id, task] : params.powTasks) {
        if (task.id != id) Fail("a task's id does not match its key");
        if (task.activation_height < 0) Fail("a task activates below height 0");
        if (task.deactivation_height && *task.deactivation_height <= task.activation_height) {
            Fail("a task deactivates before it activates");
        }
        if (task.nInitialBits == 0) Fail("a task has no anchor target");
        if (task.work_scale_den == 0 || task.work_scale_num == 0) Fail("a task has a zero work scale");
        if (arith_uint256{}.SetCompact(task.nInitialBits) > TaskPowLimit(params, task)) {
            Fail("a task's anchor is easier than its minimum difficulty");
        }
    }

    // getblocktemplate clears the bits of unsupported deployments, which would corrupt
    // the task id.
    for (int i = 0; i < static_cast<int>(Consensus::MAX_VERSION_BITS_DEPLOYMENTS); ++i) {
        const auto& deployment = params.vDeployments[static_cast<Consensus::DeploymentPos>(i)];
        if (IsPowTaskVersionBit(deployment.bit)) Fail("a deployment claims a task-id bit");
    }
}

/** Position of a task in powTasks, used to index per-block task state. */
std::optional<size_t> TaskSlot(const Consensus::Params& params, uint8_t task_id);

/** Determine whether the provided header satisfies the PoW rules for its selected task. */
bool CheckTaskProofOfWork(const CBlockHeader& header, const Consensus::Params& params);

/**
 * Run the task's solver for the header's current nonce, filling in vTaskData, and return
 * whether the header now satisfies its task. For task 0 this is just the check.
 */
bool SolveTaskProofOfWork(CBlockHeader& header, const Consensus::Params& params);

/** Locate the previous block in the same chain that solved the specified task. */
const CBlockIndex* FindPreviousTaskBlock(const CBlockIndex* start, uint8_t task_id, const Consensus::Params& params);

/** Encode per-task metadata for a block index entry (task id, spacing sums, etc.). */
void PopulateBlockTaskData(CBlockIndex& index, const Consensus::Params& params);

#endif // QUBITCOIN_POW_TASKS_H
