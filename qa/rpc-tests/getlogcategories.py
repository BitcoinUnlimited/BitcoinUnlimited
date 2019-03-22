#!/usr/bin/env python3
# Copyright (c) 2015-2019 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import test_framework.loginit
# This is a template to make creating new QA tests easy.
# You can also use this template to quickly start and connect a few regtest nodes.

import time
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging
import random

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

class GetLogCategories (BitcoinTestFramework):

    def setup_chain(self,bitcoinConfDict=None, wallets=None):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4, bitcoinConfDict, wallets)

    def setup_network(self, split=False):
        node_opts0 = ["-debug=all,-thin,-graphene,-mempool,-net"]
        node_opts1 = ["-debug=all,-thin,-graphene,-mempool,-net,-addrman,-tor,-coindb,-rpc"]
        node_opts2 = ["-debug=all,-thin,-graphene,-mempool,-net,-addrman,-tor,-coindb,-rpc,-evict,-blk,-lck,-proxy"]

        #Append rpcauth to bitcoin.conf before initialization¶
        node_opts5 = ["debug=all","debug=-thin","debug=-graphene","debug=-mempool","debug=-net"]
        random.shuffle(node_opts5)
        with open(os.path.join(self.options.tmpdir+"/node3", "bitcoin.conf"), 'a') as f:
            f.write("\n".join(node_opts5))

        self.nodes = [
            start_node(0, self.options.tmpdir, node_opts0),
            start_node(1, self.options.tmpdir, node_opts1),
            start_node(2, self.options.tmpdir, node_opts2),
            start_node(3, self.options.tmpdir)
        ]

        interconnect_nodes(self.nodes)
        self.is_network_split = False

    def run_test (self):
        exp0 = "coindb tor addrman libevent http rpc partitioncheck bench prune reindex mempoolrej blk evict parallel rand req bloom estimatefee lck proxy dbase selectcoins zmq qt ibd respend weakblocks cmpctblock"
        exp1 = "libevent http partitioncheck bench prune reindex mempoolrej blk evict parallel rand req bloom estimatefee lck proxy dbase selectcoins zmq qt ibd respend weakblocks cmpctblock"
        exp2 = "libevent http partitioncheck bench prune reindex mempoolrej parallel rand req bloom estimatefee dbase selectcoins zmq qt ibd respend weakblocks cmpctblock"
        exp3 = "libevent http partitioncheck bench prune reindex mempoolrej parallel req bloom estimatefee dbase selectcoins respend weakblocks cmpctblock"
        exp4 = exp1
        exp5 = exp0
        exp6 = ""
        exp7 = ""
        exp8 = ""


        cat0 = self.nodes[0].log()
        cat1 = self.nodes[1].log()
        cat2 = self.nodes[2].log()
        cat5 = self.nodes[3].log()
        assert_equal(cat0,exp0)
        assert_equal(cat1,exp1)
        assert_equal(cat2,exp2)
        assert_equal(cat5,exp5)

        stop_nodes(self.nodes);
        wait_bitcoinds()
        self.nodes = []

        node_opts3 = ["-debug=all,-thin,-graphene,-mempool,-net,-addrman,-tor,-coindb,-rpc,-evict,-blk,-lck,-proxy,-zmq,-qt,-ibd,-rand"]
        node_opts4 = ["-debug=-thin,-graphene,-mempool,-net,-addrman,-tor,-coindb,-rpc,all"]


        self.nodes.append(start_node(0, self.options.tmpdir, node_opts3))
        self.nodes.append(start_node(1, self.options.tmpdir, node_opts4))

        cat3 = self.nodes[0].log()
        cat4 = self.nodes[1].log()

        assert_equal(cat3,exp3)
        assert_equal(cat4,exp4)

        stop_nodes(self.nodes);
        wait_bitcoinds()
        self.nodes = []


        node_opts6 = ["-debug=-1"]
        node_opts7 = ["-debug=-"]
        node_opts8 = ["-debug=-all"]

        self.nodes.append(start_node(0, self.options.tmpdir, node_opts6))
        self.nodes.append(start_node(1, self.options.tmpdir, node_opts7))
        self.nodes.append(start_node(2, self.options.tmpdir, node_opts8))

        cat6 = self.nodes[0].log()
        cat7 = self.nodes[1].log()
        cat8 = self.nodes[2].log()
        assert_equal(cat6,exp6)
        assert_equal(cat7,exp7)
        assert_equal(cat8,exp8)

        stop_nodes(self.nodes)
        wait_bitcoinds()

if __name__ == '__main__':
    GetLogCategories().main()

# Create a convenient function for an interactive python debugging session
def Test():
    t = GetLogCategories()
    bitcoinConf = {}
    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
