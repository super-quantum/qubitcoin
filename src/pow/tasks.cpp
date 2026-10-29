// Copyright (c) 2025 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow/tasks.h>

#include <arith_uint256.h>
#include <consensus/params.h>
#include <chain.h>
#include <hash.h>
#include <pow.h>
#include <pow/tasks/deqode.h>
#include <primitives/block.h>
#include <random.h>

#include <algorithm>
#include <cassert>
#include <optional>
#include <utility>

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
    // ValidatePowTasks rules out a height with no active task.
    const auto active = std::max<size_t>(CountActiveTasks(params, height), 1);
    return params.nPowTargetSpacing * active;
}

std::optional<size_t> TaskSlot(const Consensus::Params& params, const uint8_t task_id)
{
    size_t slot = 0;
    for (const auto& entry : params.powTasks) {
        if (entry.first == task_id) return slot;
        ++slot;
    }
    return std::nullopt;
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
    case POW_DEQODE_TASK_ID:
        if (!def->deqode) return false;
        return deqode::CheckSolution(header, params, *def->deqode);
    default:
        return false;
    }
}

bool SolveTaskProofOfWork(CBlockHeader& header, const Consensus::Params& params)
{
    const uint8_t task_id = DecodeTaskId(header.nVersion);
    const auto* def = LookupTaskDefinition(params, task_id);
    if (!def) return false;

    if (task_id == POW_DEQODE_TASK_ID) {
        if (!def->deqode) return false;
        if (!deqode::ParamsAreSane(*def->deqode)) return false;

        arith_uint256 difficulty;
        if (!deqode::DifficultyFromBits(header.nBits, params, difficulty)) return false;
        deqode::DerivedWork work;
        if (!deqode::DeriveWork(difficulty, *def->deqode, work)) return false;

        const deqode::Instance instance{deqode::DeriveInstance(header, *def->deqode)};
        deqode::Witness error;
        FastRandomContext rng;
        if (!deqode::Solve(instance, work.w, rng, DEQODE_SOLVER_ITERATIONS, error)) {
            header.vTaskData.clear();
            return false;
        }
        header.vTaskData = deqode::EncodeWitness(error, def->deqode->n);
    }

    return CheckTaskProofOfWork(header, params);
}

const CBlockIndex* FindPreviousTaskBlock(const CBlockIndex* start, const uint8_t task_id, const Consensus::Params& params)
{
    if (!start) return nullptr;

    const auto slot = TaskSlot(params, task_id);
    if (!slot) {
        for (const CBlockIndex* cursor = start; cursor; cursor = cursor->pprev) {
            if (cursor->nPowTaskId == task_id) return cursor;
        }
        return nullptr;
    }

    const int32_t height = start->m_prev_task_height[*slot];
    if (height < 0) return nullptr;
    return start->GetAncestor(height);
}

void PopulateBlockTaskData(CBlockIndex& index, const Consensus::Params& params)
{
    index.nPowTaskId = DecodeTaskId(index.nVersion);

    // The parent is always populated first (AddToBlockIndex, and LoadBlockIndex walks by height).
    if (index.pprev) {
        std::copy(std::begin(index.pprev->m_prev_task_height),
                  std::end(index.pprev->m_prev_task_height),
                  std::begin(index.m_prev_task_height));
    } else {
        std::fill(std::begin(index.m_prev_task_height), std::end(index.m_prev_task_height), -1);
    }

    const auto* def = LookupTaskDefinition(params, index.nPowTaskId);
    if (!def) return;

    if (!TaskActiveAtHeight(*def, index.nHeight)) return;

    if (index.pprev) {
        const auto taskPrev = FindPreviousTaskBlock(index.pprev, index.nPowTaskId, params);
        if (taskPrev) {
            index.nTaskSpacingSum = taskPrev->nTaskSpacingSum + TargetSpacingPerTaskAtHeight(params, taskPrev->nHeight + 1);
        } else {
            index.nTaskSpacingSum = TargetSpacingPerTaskAtHeight(params, def->activation_height);
        }
    }

    if (const auto slot = TaskSlot(params, index.nPowTaskId)) {
        index.m_prev_task_height[*slot] = index.nHeight;
    }
}
