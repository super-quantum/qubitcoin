// Copyright (c) 2025 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef QUBITCOIN_POW_TASKS_H
#define QUBITCOIN_POW_TASKS_H

#include <consensus/params.h>

#include <cstdint>
#include <optional>

class CBlockHeader;
class CBlockIndex;
class BlockValidationState;

static constexpr int POW_TASK_ID_SHIFT = 20;
static constexpr uint32_t POW_TASK_ID_MASK = 0xFFu << POW_TASK_ID_SHIFT;
static constexpr uint8_t POW_DEFAULT_TASK_ID = 0;

/** Extract the task id encoded in the block version (bits 20-27). */
uint8_t DecodeTaskId(int32_t nVersion);

/** Replace the task id bits in the supplied version. */
int32_t EncodeTaskId(int32_t nVersion, uint8_t task_id);

/** Look up the consensus definition for a given task id. */
const Consensus::Params::PowTaskDefinition* LookupTaskDefinition(const Consensus::Params& params, uint8_t task_id);

/** Return true if the specified task may be used at the provided height. */
bool IsTaskActive(const Consensus::Params& params, uint8_t task_id, int height);

/** Count the number of tasks that are active at the provided height. */
size_t CountActiveTasks(const Consensus::Params& params, int height);

/** Return the current per-task target spacing (in seconds) for a block at the provided height. */
int64_t TargetSpacingPerTaskAtHeight(const Consensus::Params& params, int height);

/** Determine whether the provided header satisfies the PoW rules for its selected task. */
bool CheckTaskProofOfWork(const CBlockHeader& header, const Consensus::Params& params);

/** Locate the previous block in the same chain that solved the specified task. */
const CBlockIndex* FindPreviousTaskBlock(const CBlockIndex* start, uint8_t task_id);

/** Encode per-task metadata for a block index entry (task id, spacing sums, etc.). */
void PopulateBlockTaskData(CBlockIndex& index, const Consensus::Params& params);

#endif // QUBITCOIN_POW_TASKS_H
