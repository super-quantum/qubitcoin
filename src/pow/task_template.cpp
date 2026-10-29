// Copyright (c) 2025 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow/task_template.h>

#include <consensus/params.h>
#include <pow/task_id.h>
#include <pow/tasks.h>

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
    default:
        task.pushKV("data_bytes", 0);
        task.pushKV("unsupported", true);
        break;
    }

    return task;
}
