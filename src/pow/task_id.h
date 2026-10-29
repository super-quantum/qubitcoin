// Copyright (c) 2025 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef QUBITCOIN_POW_TASK_ID_H
#define QUBITCOIN_POW_TASK_ID_H

#include <cstdint>

/**
 * A version whose top three bits are 010 carries a task id in bits 20-27; any other version
 * is task 0. Bits 20-27 are also reserved out of BIP9 (see IsPowTaskVersionBit).
 */
static constexpr int POW_TASK_ID_SHIFT = 20;
static constexpr int POW_TASK_ID_BITS = 8;
static constexpr uint32_t POW_TASK_ID_MASK = 0xFFu << POW_TASK_ID_SHIFT;
static constexpr uint32_t POW_TASK_VERSION_MARKER_MASK = 0xE0000000u;
static constexpr uint32_t POW_TASK_VERSION_MARKER = 0x40000000u;
static constexpr uint8_t POW_DEFAULT_TASK_ID = 0;

/** True if this version bit is reserved for the proof-of-work task id. */
constexpr bool IsPowTaskVersionBit(const int bit)
{
    return bit >= POW_TASK_ID_SHIFT && bit < POW_TASK_ID_SHIFT + POW_TASK_ID_BITS;
}

/** Extract the task id encoded in the block version. */
inline uint8_t DecodeTaskId(const int32_t nVersion)
{
    const uint32_t version{static_cast<uint32_t>(nVersion)};
    if ((version & POW_TASK_VERSION_MARKER_MASK) != POW_TASK_VERSION_MARKER) return POW_DEFAULT_TASK_ID;
    return (version & POW_TASK_ID_MASK) >> POW_TASK_ID_SHIFT;
}

/** Set the task id in the supplied version. Task 0 leaves an unmarked version unchanged. */
inline int32_t EncodeTaskId(const int32_t nVersion, const uint8_t task_id)
{
    const uint32_t version{static_cast<uint32_t>(nVersion)};
    if (task_id == POW_DEFAULT_TASK_ID) {
        if ((version & POW_TASK_VERSION_MARKER_MASK) != POW_TASK_VERSION_MARKER) return nVersion;
        return version & ~POW_TASK_ID_MASK;
    }
    return (version & ~(POW_TASK_VERSION_MARKER_MASK | POW_TASK_ID_MASK)) | POW_TASK_VERSION_MARKER |
           (static_cast<uint32_t>(task_id) << POW_TASK_ID_SHIFT);
}

#endif // QUBITCOIN_POW_TASK_ID_H
