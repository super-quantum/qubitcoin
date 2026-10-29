// Copyright (c) 2025 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef QUBITCOIN_POW_TASK_TEMPLATE_H
#define QUBITCOIN_POW_TASK_TEMPLATE_H

#include <univalue.h>

#include <cstdint>

namespace Consensus { struct Params; }

/**
 * The "task" object of a getblocktemplate response: "id", "name", "data_bytes" (the
 * expected vTaskData length) and any task-specific fields in an object named after the
 * task. nBits is the template's, since a task may derive its acceptance rule from it.
 */
UniValue TaskTemplateFields(const Consensus::Params& params, uint8_t task_id, uint32_t nBits);

#endif // QUBITCOIN_POW_TASK_TEMPLATE_H
