#!/usr/bin/env python3
# Copyright (c) 2021 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
# You coud use it this test to check various kind of init error
# The first iteration of this test verify that bitcoind properly error out
# during the startup phase if you pass a non existent path as value to the
# datadir parameter

import time
import sys
import logging
import os

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class CmdLineTest(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = True
        self.num_nodes = 1

    def run_test(self):
        datadir = os.path.join(self.options.tmpdir, str("non_existant_path"))
        start_node_and_raise_on_init_error(0, datadir, "is not a directory or it does not exist")

if __name__ == '__main__':
    CmdLineTest().main()

# Create a convenient function for an interactive python debugging session
def Test():
    t = CmdLineTest()
    # t.drop_to_pdb = True
    bitcoinConf = {
        "debug": [],
    }


    flags = []
    t.main(flags, bitcoinConf, None)
