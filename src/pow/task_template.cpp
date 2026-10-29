// Copyright (c) 2025 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow/task_template.h>

#include <arith_uint256.h>
#include <consensus/params.h>
#include <pow/task_id.h>
#include <pow/tasks.h>
#include <pow/tasks/deqode.h>
#include <uint256.h>

UniValue TaskTemplateFields(const Consensus::Params& params, const uint8_t task_id, const uint32_t nBits)
{
    const auto* def = LookupTaskDefinition(params, task_id);
    if (!def) return UniValue{UniValue::VNULL};

    UniValue task{UniValue::VOBJ};
    task.pushKV("id", (int)task_id);
    task.pushKV("name", def->name);

    switch (task_id) {
    case POW_DEFAULT_TASK_ID:
        task.pushKV("data_bytes", 0);
        break;
    case POW_DEQODE_TASK_ID: {
        if (!def->deqode) {
            task.pushKV("data_bytes", 0);
            task.pushKV("unsupported", true);
            break;
        }
        const auto& sizing = *def->deqode;
        const int witness_bytes{2 * ((sizing.n + 7) / 8)};
        task.pushKV("data_bytes", witness_bytes);

        UniValue deqode{UniValue::VOBJ};
        deqode.pushKV("n", sizing.n);
        deqode.pushKV("r", sizing.r);
        deqode.pushKV("gate_multiplier", sizing.gate_multiplier);
        deqode.pushKV("code_seed", "hashPrevBlock");
        deqode.pushKV("syndrome_seed", "sha256(header[0:80])");
        deqode.pushKV("acceptance_preimage", "header[0:80] || witness");

        arith_uint256 difficulty;
        deqode::DerivedWork work;
        if (deqode::DifficultyFromBits(nBits, params, difficulty) &&
            deqode::DeriveWork(difficulty, sizing, work)) {
            // These depend on the template's nBits.
            deqode.pushKV("w", work.w);
            deqode.pushKV("expected_witnesses", (uint64_t)work.k);
            deqode.pushKV("acceptance_target", ArithToUint256(work.target).GetHex());
            task.pushKV("deqode", std::move(deqode));
        } else {
            // The hardness table cannot express this difficulty.
            task.pushKV("deqode", std::move(deqode));
            task.pushKV("unsupported", true);
        }
        break;
    }
    default:
        task.pushKV("data_bytes", 0);
        task.pushKV("unsupported", true);
        break;
    }

    return task;
}
