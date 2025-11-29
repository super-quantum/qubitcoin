// Copyright (c) 2025 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow/tasks.h>

#include <consensus/params.h>
#include <chain.h>
#include <hash.h>
#include <pow.h>
#include <primitives/block.h>
#include <validation.h>

#include <algorithm>
#include <cassert>
#include <utility>

uint8_t DecodeTaskId(const int32_t nVersion)
{
    return (static_cast<uint32_t>(nVersion) & POW_TASK_ID_MASK) >> POW_TASK_ID_SHIFT;
}

int32_t EncodeTaskId(const int32_t nVersion, const uint8_t task_id)
{
    return (static_cast<uint32_t>(nVersion) & ~POW_TASK_ID_MASK) | (static_cast<uint32_t>(task_id) << POW_TASK_ID_SHIFT);
}

static bool TaskActiveAtHeight(const Consensus::Params::PowTaskDefinition& def, const int height)
{
    if (height < def.activation_height) return false;
    if (def.deactivation_height && height >= *def.deactivation_height) return false;
    return true;
}

const Consensus::Params::PowTaskDefinition* LookupTaskDefinition(const Consensus::Params& params, const uint8_t task_id)
{
    const auto it = params.powTasks.find(task_id);
    if (it == params.powTasks.end()) return nullptr;
    return &it->second;
}

bool IsTaskActive(const Consensus::Params& params, const uint8_t task_id, const int height)
{
    const auto* def = LookupTaskDefinition(params, task_id);
    if (!def) return false;
    return TaskActiveAtHeight(*def, height);
}

size_t CountActiveTasks(const Consensus::Params& params, const int height)
{
    size_t count = 0;
    for (const auto& entry : params.powTasks) {
        if (TaskActiveAtHeight(entry.second, height)) {
            ++count;
        }
    }
    return count;
}

int64_t TargetSpacingPerTaskAtHeight(const Consensus::Params& params, const int height)
{
    const auto active = CountActiveTasks(params, height);
    assert(active > 0);
    return params.nPowTargetSpacing * active;
}

static bool CheckQhashSolution(const CBlockHeader& header, const Consensus::Params& params)
{
    const uint256 digest = (QHashWriter{header.nTime} << header).GetHash();
    return CheckProofOfWork(digest, header.nBits, params);
}

bool CheckTaskProofOfWork(const CBlockHeader& header, const Consensus::Params& params)
{
    const uint8_t task_id = DecodeTaskId(header.nVersion);
    const auto* def = LookupTaskDefinition(params, task_id);
    if (!def) return false;
    switch (task_id) {
    case POW_DEFAULT_TASK_ID:
        return CheckQhashSolution(header, params);
    default:
        return false;
    }
}

const CBlockIndex* FindPreviousTaskBlock(const CBlockIndex* start, const uint8_t task_id)
{
    const CBlockIndex* cursor = start;
    while (cursor) {
        if (cursor->nPowTaskId == task_id) return cursor;
        cursor = cursor->pprev;
    }
    return nullptr;
}

void PopulateBlockTaskData(CBlockIndex& index, const Consensus::Params& params)
{
    index.nPowTaskId = DecodeTaskId(index.nVersion);

    const int height = index.nHeight;
    const auto* def = LookupTaskDefinition(params, index.nPowTaskId);
    if (!def) return;
    if (!TaskActiveAtHeight(*def, height)) return;


    if (index.pprev) {
        const auto taskPrev = FindPreviousTaskBlock(index.pprev, index.nPowTaskId);
        if (taskPrev) 
            index.nTaskSpacingSum = taskPrev->nTaskSpacingSum + TargetSpacingPerTaskAtHeight(params, taskPrev->nHeight + 1);
        else
            index.nTaskSpacingSum = TargetSpacingPerTaskAtHeight(params, def->activation_height);
    }

}
