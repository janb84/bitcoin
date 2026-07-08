#!/usr/bin/env python3
# Copyright (c) 2026-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Contract test for patch 01-remove-gui: the Qt GUI is globally absent.

This test encodes the patch's invariants (patches/01-remove-gui/SPEC.md) and
must FAIL on an unpatched upstream tree (negative control, FORK_WORKFLOW.md §5).
"""
import os

from test_framework.test_framework import BitcoinTestFramework


class ForkNoGuiTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 0  # Source-tree contract test; no node needed
        self.setup_clean_chain = True  # Never build the chain cache; runs on a configure-only tree

    def setup_network(self):
        pass

    def run_test(self):
        srcdir = self.config["environment"]["SRCDIR"]
        builddir = self.config["environment"]["BUILDDIR"]

        self.log.info("Check that the GUI source tree is gone")
        qt_dir = os.path.join(srcdir, "src", "qt")
        assert not os.path.exists(qt_dir), f"GUI source tree present: {qt_dir}"

        self.log.info("Check that the build system offers no GUI options")
        for cmake_file in ("CMakeLists.txt", os.path.join("src", "CMakeLists.txt")):
            with open(os.path.join(srcdir, cmake_file), encoding="utf8") as f:
                content = f.read()
            for symbol in ("BUILD_GUI", "bitcoin-qt", "WITH_QRENCODE", "WITH_DBUS"):
                assert symbol not in content, f"{symbol} referenced in {cmake_file}"

        self.log.info("Check that depends carries no Qt/qrencode packages")
        packages_dir = os.path.join(srcdir, "depends", "packages")
        for pkg in ("qt.mk", "qt_details.mk", "native_qt.mk", "qrencode.mk"):
            path = os.path.join(packages_dir, pkg)
            assert not os.path.exists(path), f"GUI depends package present: {path}"

        self.log.info("Check that no bitcoin-qt binary was built")
        for candidate in (
            os.path.join(builddir, "bin", "bitcoin-qt"),
            os.path.join(builddir, "bin", "bitcoin-qt.exe"),
        ):
            assert not os.path.exists(candidate), f"GUI binary present: {candidate}"


if __name__ == '__main__':
    ForkNoGuiTest(__file__).main()
