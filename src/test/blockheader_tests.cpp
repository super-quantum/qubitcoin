// Copyright (c) 2025 Superquantum
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <hash.h>
#include <headerssync.h>
#include <pow/task_id.h>
#include <primitives/block.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <ios>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(blockheader_tests, BasicTestingSetup)

namespace {
CBlockHeader MakeHeader(const uint8_t task_id)
{
    CBlockHeader header;
    header.nVersion = EncodeTaskId(0x20000000, task_id);
    header.hashPrevBlock = uint256S("0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20");
    header.hashMerkleRoot = uint256S("2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40");
    header.nTime = 0x11223344;
    header.nBits = 0x207ffffe;
    header.nNonce = 0x55667788;
    return header;
}

std::string HexHeader(const CBlockHeader& header)
{
    DataStream ss;
    ss << header;
    return HexStr(ss);
}
} // namespace

BOOST_AUTO_TEST_CASE(task_id_encoding)
{
    // Only versions whose top bits are 010 carry a task id. Mainnet has blocks with
    // version 0x04000020, which must stay task 0.
    for (const int32_t version : {0x04000020, 0x20ff0000, 0x7ff00000, int32_t(0xc0100000)}) {
        CBlockHeader header{MakeHeader(POW_DEFAULT_TASK_ID)};
        header.nVersion = version;
        BOOST_CHECK_EQUAL(DecodeTaskId(version), POW_DEFAULT_TASK_ID);
        BOOST_CHECK_EQUAL(GetSerializeSize(header), 80U);
    }
    BOOST_CHECK_EQUAL(DecodeTaskId(0x40100000), 1);
    BOOST_CHECK_EQUAL(DecodeTaskId(0x5ff00000), 255);

    BOOST_CHECK_EQUAL(EncodeTaskId(0x20000004, 0), 0x20000004);
    BOOST_CHECK_EQUAL(EncodeTaskId(0x20000004, 1), 0x40100004);
    BOOST_CHECK_EQUAL(EncodeTaskId(0x40100004, 0), 0x40000004);
    BOOST_CHECK_EQUAL(EncodeTaskId(0x04000020, 7), 0x40700020);
}

BOOST_AUTO_TEST_CASE(default_task_header)
{
    CBlockHeader header{MakeHeader(POW_DEFAULT_TASK_ID)};
    BOOST_CHECK_EQUAL(GetSerializeSize(header), 80U);
    const std::string hex_without{HexHeader(header)};

    // Task data is not serialized for task 0.
    header.vTaskData = {0xde, 0xad, 0xbe, 0xef};
    BOOST_CHECK_EQUAL(HexHeader(header), hex_without);

    DataStream ss;
    ss << header;
    CBlockHeader restored;
    ss >> restored;
    BOOST_CHECK(ss.empty());
    BOOST_CHECK(restored.vTaskData.empty());
}

BOOST_AUTO_TEST_CASE(task_header_serialization)
{
    CBlockHeader header{MakeHeader(1)};
    header.vTaskData = {0x01, 0x02, 0x03, 0x04, 0x05};
    BOOST_CHECK_EQUAL(GetSerializeSize(header), 80U + 1U + header.vTaskData.size());

    DataStream ss;
    ss << header;
    CBlockHeader restored;
    ss >> restored;
    BOOST_CHECK(ss.empty());
    BOOST_CHECK_EQUAL(HexHeader(restored), HexHeader(header));
    BOOST_CHECK(restored.vTaskData == header.vTaskData);

    CBlock block{header};
    BOOST_CHECK(block.GetBlockHeader().vTaskData == header.vTaskData);
    DataStream block_ss;
    block_ss << TX_WITH_WITNESS(block);
    CBlock restored_block;
    block_ss >> TX_WITH_WITNESS(restored_block);
    BOOST_CHECK(restored_block.vTaskData == header.vTaskData);

    header.vTaskData.assign(MAX_BLOCK_TASK_DATA_SIZE, 0xcd);
    DataStream at_cap;
    at_cap << header;
    at_cap >> restored;
    BOOST_CHECK_EQUAL(restored.vTaskData.size(), MAX_BLOCK_TASK_DATA_SIZE);

    header.vTaskData.assign(MAX_BLOCK_TASK_DATA_SIZE + 1, 0xcd);
    DataStream too_big;
    too_big << header;
    BOOST_CHECK_THROW(too_big >> restored, std::ios_base::failure);
}

BOOST_AUTO_TEST_CASE(disk_block_index_task_data)
{
    CBlockHeader header{MakeHeader(1)};
    header.vTaskData = {0xaa, 0xbb, 0xcc};

    CBlockIndex index{header};
    BOOST_CHECK(index.GetBlockHeader().vTaskData == header.vTaskData);

    CDiskBlockIndex disk{&index};
    DataStream ss;
    ss << disk;
    CDiskBlockIndex restored;
    ss >> restored;
    BOOST_CHECK(ss.empty());
    BOOST_CHECK(restored.vTaskData == header.vTaskData);

    // A task-0 entry keeps the old on-disk format.
    CBlockIndex default_index{MakeHeader(POW_DEFAULT_TASK_ID)};
    CDiskBlockIndex default_disk{&default_index};
    BOOST_CHECK_EQUAL(GetSerializeSize(disk), GetSerializeSize(default_disk) + 1U + header.vTaskData.size());
}

BOOST_AUTO_TEST_CASE(compressed_header_task_data)
{
    for (const uint8_t task_id : {POW_DEFAULT_TASK_ID, uint8_t{1}}) {
        CBlockHeader header{MakeHeader(task_id)};
        if (task_id != POW_DEFAULT_TASK_ID) header.vTaskData = {0x11, 0x22, 0x33, 0x44};

        CompressedHeader compressed{header};
        const CBlockHeader restored{compressed.GetFullHeader(header.hashPrevBlock)};
        BOOST_CHECK_EQUAL(HexHeader(restored), HexHeader(header));
        BOOST_CHECK(restored.vTaskData == header.vTaskData);
    }

    BOOST_CHECK_EQUAL(CompressedHeader{CBlockHeader{}}.MemoryUsage(), sizeof(CompressedHeader));
    CBlockHeader largest{MakeHeader(1)};
    largest.vTaskData.assign(MAX_BLOCK_TASK_DATA_SIZE, 0xcd);
    BOOST_CHECK_LE(CompressedHeader{largest}.MemoryUsage(), MAX_COMPRESSED_HEADER_BYTES);
}

BOOST_AUTO_TEST_CASE(task_header_hash)
{
    CBlockHeader a{MakeHeader(1)};
    a.vTaskData = {0x01, 0x02, 0x03, 0x04};
    CBlockHeader b{a};
    b.vTaskData = {0x01, 0x02, 0x03, 0x05};
    BOOST_CHECK(a.GetHash() != b.GetHash());

    // Only task 0 uses the time-gated qhash rules.
    BOOST_CHECK(a.GetHash() == (QHashWriter{a.nTime, QHashRules::Current} << a).GetHash());
    BOOST_CHECK(a.GetHash() != (QHashWriter{a.nTime} << a).GetHash());

    const CBlockHeader plain{MakeHeader(POW_DEFAULT_TASK_ID)};
    BOOST_CHECK(plain.GetHash() == (QHashWriter{plain.nTime} << plain).GetHash());
    BOOST_CHECK(plain.GetHash() != (QHashWriter{plain.nTime, QHashRules::Current} << plain).GetHash());
}

//! The degenerate all-ones digest is only substituted for task 0 headers.
BOOST_AUTO_TEST_CASE(degenerate_digest)
{
    const uint256 degenerate{uint256S("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff")};
    // After the last degenerate-output activation time.
    const uint32_t nTime{1800000000};

    int task_checked{0};
    int default_checked{0};
    for (uint32_t nonce = 0; nonce < 400 && (task_checked == 0 || default_checked == 0); ++nonce) {
        CBlockHeader header{MakeHeader(1)};
        header.nTime = nTime;
        header.nNonce = nonce;
        header.vTaskData = {0x11, 0x22, 0x33, 0x44};
        if (task_checked == 0 && (QHashWriter{header.nTime} << header).GetHash() == degenerate) {
            BOOST_CHECK(header.GetHash() != degenerate);
            ++task_checked;
        }

        CBlockHeader plain{MakeHeader(POW_DEFAULT_TASK_ID)};
        plain.nTime = nTime;
        plain.nNonce = nonce;
        if (default_checked == 0 && (QHashWriter{plain.nTime} << plain).GetHash() == degenerate) {
            BOOST_CHECK(plain.GetHash() == degenerate);
            ++default_checked;
        }
    }
    BOOST_CHECK_GT(task_checked, 0);
    BOOST_CHECK_GT(default_checked, 0);
}

BOOST_AUTO_TEST_SUITE_END()
