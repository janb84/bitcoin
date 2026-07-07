#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Contract test for patch 00-fork-conventions: the fork_ test namespace exists.

This test encodes the patch's invariants (patches/00-fork-conventions/SPEC.md)
and must FAIL on an unpatched upstream tree (negative control,
FORK_WORKFLOW.md §5).
"""
import importlib.util
import os

from test_framework.test_framework import BitcoinTestFramework


class ForkConventionsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 0  # Source-tree contract test; no node needed
        self.setup_clean_chain = True  # Never build the chain cache; runs on a configure-only tree

    def setup_network(self):
        pass

    def run_test(self):
        srcdir = self.config["environment"]["SRCDIR"]
        spec = importlib.util.spec_from_file_location(
            "test_runner", os.path.join(srcdir, "test", "functional", "test_runner.py"))
        assert spec is not None and spec.loader is not None
        test_runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(test_runner)

        self.log.info("Check that the runner's naming check accepts the fork_ namespace")
        registered_scripts = test_runner.ALL_SCRIPTS
        test_runner.ALL_SCRIPTS = ["fork_example.py"]
        test_runner.check_script_prefixes()  # raises if fork_ is not an accepted prefix

        self.log.info("Check that this contract test is registered with the runner")
        assert "fork_conventions.py" in registered_scripts


if __name__ == '__main__':
    ForkConventionsTest(__file__).main()
