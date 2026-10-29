#!/usr/bin/env python3
# Copyright (c) 2025 Superquantum
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the deqode proof-of-work task (task 1) on regtest, enabled with -deqodeheight."""

import hashlib
import random

from test_framework import deqode
from test_framework.blocktools import (
    add_witness_commitment,
    create_block,
    create_coinbase,
)
from test_framework.messages import (
    BLOCK_HEADER_SIZE,
    CBlock,
    CBlockHeader,
    decode_task_id,
    from_hex,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_node import ErrorMatch
from test_framework.util import (
    assert_equal,
    assert_greater_than,
    assert_greater_than_or_equal,
    assert_raises_rpc_error,
)

DEFAULT_TASK_ID = 0
DEQODE_TASK_ID = 1

# n = 32, so the witness is 2 * ceil(32/8) = 8 bytes.
DEQODE_WITNESS_BYTES = 8

# DEQODE_K_MIN / DEQODE_K_MAX
K_MIN = 4
K_MAX = 92

DESCRIPTOR = "raw(51)"


def build_block_from_template(template, task_id):
    """The unsolved block a miner would build from this template."""
    coinbase = create_coinbase(height=template["height"])
    coinbase.vout[0].nValue = template["coinbasevalue"]
    coinbase.rehash()
    block = create_block(tmpl=template, coinbase=coinbase, version=template["version"],
                         txlist=[tx["data"] for tx in template["transactions"]])
    add_witness_commitment(block)
    assert_equal(decode_task_id(block.nVersion), task_id)
    return block


class MiningDeqodeTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.supports_cli = False
        self.extra_args = [["-deqodeheight=0"], ["-deqodeheight=0"]]

    def mine(self, task_id, node=0, sync_fun=None):
        """Mine one block solving the given task and return its hash."""
        hashes = self.generatetodescriptor(
            self.nodes[node], 1, DESCRIPTOR, 1000000, task_id,
            sync_fun=sync_fun or self.no_op)
        assert_equal(len(hashes), 1)
        return hashes[0]

    def test_tasks_are_listed_before_any_block(self):
        self.log.info("Both tasks are reported before either has produced a block")
        for rpc in (self.nodes[0].getmininginfo(), self.nodes[0].getblockchaininfo()):
            tasks = {task["id"]: task for task in rpc["tasks"]}
            assert_equal(sorted(tasks), [DEFAULT_TASK_ID, DEQODE_TASK_ID])
            assert_equal(tasks[DEQODE_TASK_ID]["name"], "deqode")
            assert tasks[DEQODE_TASK_ID]["active"]
            assert_greater_than(tasks[DEQODE_TASK_ID]["difficulty"], 0)

        deqode_task = {task["id"]: task for task in self.nodes[0].getmininginfo()["tasks"]}[DEQODE_TASK_ID]
        assert_greater_than(deqode_task["weight_bound"], 0)
        assert_greater_than_or_equal(deqode_task["expected_witnesses"], K_MIN)
        assert_greater_than_or_equal(K_MAX, deqode_task["expected_witnesses"])

        assert_greater_than(self.nodes[0].getdifficulty(DEQODE_TASK_ID), 0)
        assert_raises_rpc_error(-8, "Task ID 9 does not exist", self.nodes[0].getdifficulty, 9)

    def test_getblocktemplate_task_id(self):
        self.log.info("getblocktemplate defaults to the default task and accepts an explicit one")
        node = self.nodes[0]

        template = node.getblocktemplate({"rules": ["segwit"]})
        assert_equal(template["task_id"], DEFAULT_TASK_ID)
        assert_equal(template["task_name"], "qhash")

        explicit = node.getblocktemplate({"rules": ["segwit"], "task_id": DEFAULT_TASK_ID})
        assert_equal(explicit["task_id"], DEFAULT_TASK_ID)

        deqode_tmpl = node.getblocktemplate({"rules": ["segwit"], "task_id": DEQODE_TASK_ID})
        assert_equal(deqode_tmpl["task_id"], DEQODE_TASK_ID)
        assert_equal(deqode_tmpl["task_name"], "deqode")
        assert_equal(decode_task_id(deqode_tmpl["version"]), DEQODE_TASK_ID)

        assert_raises_rpc_error(-8, "task_id must be between 0 and 255",
                                node.getblocktemplate, {"rules": ["segwit"], "task_id": 300})
        assert_raises_rpc_error(-8, "Task ID 9 does not exist",
                                node.getblocktemplate, {"rules": ["segwit"], "task_id": 9})

    def test_mine_deqode_from_a_template(self):
        self.log.info("an outside miner can solve a task-1 template and submit the block")
        node = self.nodes[0]

        template = node.getblocktemplate({"rules": ["segwit"], "task_id": DEQODE_TASK_ID})
        sizing = template["task"]["deqode"]
        witness_bytes = template["task"]["data_bytes"]
        assert_equal(witness_bytes, 2 * ((sizing["n"] + 7) // 8))
        assert "taskdata" in template["mutable"]
        assert "unsupported" not in template["task"]

        weight_bound = sizing["w"]
        target = int(sizing["acceptance_target"], 16)
        assert_greater_than_or_equal(sizing["expected_witnesses"], K_MIN)
        assert_greater_than_or_equal(K_MAX, sizing["expected_witnesses"])
        assert_greater_than(target, int(template["target"], 16))

        block = build_block_from_template(template, DEQODE_TASK_ID)
        rng = random.Random(0)

        solved = False
        for nonce in range(256):
            block.nNonce = nonce
            col_x, col_z, syn = deqode.instance_from_header(block, sizing)
            error = deqode.solve(col_x, col_z, syn, sizing["n"], weight_bound, rng)
            if error is None:
                continue

            witness = deqode.encode_witness(error[0], error[1], sizing["n"])
            prefix = deqode.header_prefix80(block)
            acceptance = int.from_bytes(hashlib.sha256(prefix + witness).digest(), "little")
            if acceptance >= target:
                continue

            assert_equal(len(witness), witness_bytes)
            assert_equal(deqode.syndrome(col_x, col_z, error[0], error[1], sizing["n"]), syn)
            assert deqode.pauli_weight(*error) <= weight_bound

            block.vTaskData = witness
            solved = True
            break

        assert solved, "no task-1 solution found within the nonce budget"

        height_before = node.getblockcount()
        assert_equal(node.submitblock(block.serialize().hex()), None)
        assert_equal(node.getblockcount(), height_before + 1)

        tip = node.getblock(node.getbestblockhash())
        assert_equal(tip["taskdata"], block.vTaskData.hex())
        assert_equal(decode_task_id(tip["version"]), DEQODE_TASK_ID)

    def test_mine_both_tasks(self):
        self.log.info("Mine a block for each task and check what the header carries")
        task0_hash = self.mine(DEFAULT_TASK_ID)
        task1_hash = self.mine(DEQODE_TASK_ID)
        assert task0_hash != task1_hash

        task0 = self.nodes[0].getblockheader(task0_hash)
        assert_equal(decode_task_id(task0["version"]), DEFAULT_TASK_ID)
        assert "taskdata" not in task0

        task1 = self.nodes[0].getblockheader(task1_hash)
        assert_equal(decode_task_id(task1["version"]), DEQODE_TASK_ID)
        assert_equal(len(task1["taskdata"]) // 2, DEQODE_WITNESS_BYTES)

        self.log.info("The framework round-trips both headers")
        raw0 = self.nodes[0].getblockheader(task0_hash, False)
        assert_equal(len(raw0) // 2, BLOCK_HEADER_SIZE)
        header0 = from_hex(CBlockHeader(), raw0)
        assert_equal(header0.vTaskData, b"")
        assert_equal(header0.serialize().hex(), raw0)

        raw1 = self.nodes[0].getblockheader(task1_hash, False)
        assert_equal(len(raw1) // 2, BLOCK_HEADER_SIZE + 1 + DEQODE_WITNESS_BYTES)
        header1 = from_hex(CBlockHeader(), raw1)
        assert_equal(header1.vTaskData.hex(), task1["taskdata"])
        assert_equal(header1.serialize().hex(), raw1)

        return task1_hash

    def test_every_block_adds_the_same_chain_work(self):
        self.log.info("Every block adds the same chain work, whichever task solved it")
        # Regtest does not retarget, and task 1's work scale makes both anchors worth 2.
        node = self.nodes[0]
        previous = int(node.getblockheader(node.getblockhash(0))["chainwork"], 16)
        tasks = set()
        for height in range(1, node.getblockcount() + 1):
            header = node.getblockheader(node.getblockhash(height))
            tasks.add(decode_task_id(header["version"]))
            work = int(header["chainwork"], 16)
            assert_equal(work - previous, 2)
            previous = work
        assert_equal(tasks, {DEFAULT_TASK_ID, DEQODE_TASK_ID})

    def test_blocks_relay(self):
        self.log.info("Task-1 blocks relay to a second node")
        self.connect_nodes(0, 1)
        self.sync_all()
        assert_equal(self.nodes[1].getbestblockhash(), self.nodes[0].getbestblockhash())
        assert_equal(self.nodes[1].getblockcount(), self.nodes[0].getblockcount())

    def test_corrupt_witness_is_rejected(self, task1_hash):
        self.log.info("A block whose witness is corrupted is rejected")
        block_hex = self.nodes[0].getblock(task1_hash, 0)
        block = from_hex(CBlock(), block_hex)
        assert_equal(len(block.vTaskData), DEQODE_WITNESS_BYTES)

        def submit(mutate, reason="high-hash"):
            """Mutate a copy of the good block and require the node to refuse it."""
            candidate = from_hex(CBlock(), block_hex)
            mutate(candidate)
            candidate.rehash()
            assert_equal(self.nodes[0].submitblock(candidate.serialize().hex()), reason)

        submit(lambda b: setattr(b, "vTaskData", bytes([b.vTaskData[0] ^ 0x01]) + b.vTaskData[1:]))
        submit(lambda b: setattr(b, "vTaskData", b.vTaskData[:-1]))
        submit(lambda b: setattr(b, "vTaskData", b.vTaskData + b"\x00"))
        submit(lambda b: setattr(b, "vTaskData", b""), "missing-task-data")
        assert_equal(self.nodes[0].getbestblockhash(), task1_hash)

    def test_default_regtest_has_one_task(self):
        self.log.info("Without -deqodeheight regtest sees exactly one task")
        self.restart_node(0, extra_args=[])
        tasks = self.nodes[0].getmininginfo()["tasks"]
        assert_equal([task["id"] for task in tasks], [DEFAULT_TASK_ID])
        assert_raises_rpc_error(-8, "Task ID 1 does not exist", self.nodes[0].getdifficulty, 1)
        self.restart_node(0, extra_args=["-deqodeheight=0"])

    def test_unregistered_task_makes_the_chain_unloadable(self):
        self.log.info("A chain containing task-1 blocks will not load without the task")
        self.stop_node(0)
        self.nodes[0].assert_start_raises_init_error(
            extra_args=[], expected_msg=": Error loading block database.",
            match=ErrorMatch.PARTIAL_REGEX)
        self.start_node(0, extra_args=["-deqodeheight=0"])

    def run_test(self):
        self.test_default_regtest_has_one_task()
        self.test_tasks_are_listed_before_any_block()
        self.test_getblocktemplate_task_id()
        self.test_mine_deqode_from_a_template()
        task1_hash = self.test_mine_both_tasks()
        self.test_blocks_relay()
        self.test_every_block_adds_the_same_chain_work()
        self.test_corrupt_witness_is_rejected(task1_hash)
        self.test_unregistered_task_makes_the_chain_unloadable()


if __name__ == "__main__":
    MiningDeqodeTest().main()
