#!/usr/bin/env python3
# Copyright (c) 2025 Superquantum
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the task fields getblocktemplate publishes for out-of-node miners."""

from test_framework.address import ADDRESS_BCRT1_UNSPENDABLE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

DEFAULT_TASK_ID = 0


class MiningTaskTemplateTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]

        self.log.info("the default task publishes a witness-free contract")
        template = node.getblocktemplate({"rules": ["segwit"]})
        assert_equal(template["task_id"], DEFAULT_TASK_ID)
        assert_equal(template["task_name"], "qhash")
        assert_equal(template["task"]["id"], DEFAULT_TASK_ID)
        assert_equal(template["task"]["name"], "qhash")
        assert_equal(template["task"]["data_bytes"], 0)

        self.log.info("a witness-free task does not advertise taskdata as mutable")
        assert "taskdata" not in template["mutable"]

        self.log.info("an undefined task is still rejected")
        assert_raises_rpc_error(-8, "Task ID 9 does not exist",
                                node.getblocktemplate, {"rules": ["segwit"], "task_id": 9})
        assert_raises_rpc_error(-8, "task_id must be between 0 and 255",
                                node.getblocktemplate, {"rules": ["segwit"], "task_id": 300})

        self.log.info("a block mined the normal way still round-trips through submitblock")
        self.generatetoaddress(node, 1, ADDRESS_BCRT1_UNSPENDABLE)
        best = node.getbestblockhash()
        raw = node.getblock(best, 0)
        assert_equal(node.submitblock(raw), "duplicate")


if __name__ == "__main__":
    MiningTaskTemplateTest().main()
