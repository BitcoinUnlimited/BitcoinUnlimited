#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Unlimited developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
import test_framework.loginit
from test_framework.util import sync_blocks
from test_framework.util import *
from test_framework.mininode import CTransaction, NetworkThread
from test_framework.blocktools import create_coinbase, create_block
from test_framework.script import CScript, OP_1NEGATE, OP_CHECKSEQUENCEVERIFY, OP_DROP
from test_framework.test_framework import BitcoinTestFramework

from io import BytesIO
import time
import itertools
import tempfile

'''
This test exercises BIP135 fork settings.
It uses a single node with custom forks.csv file.
'''


class BIP135VoteTest(BitcoinTestFramework):

    def __init__(self):
        super().__init__()
        self.setup_clean_chain = False
        self.num_nodes = 1
        self.defined_forks = ["bip135test%d" % i for i in range(0, 8)]

    def setup_network(self):
        '''
        sets up with a custom forks.csv to test threshold / grace conditions
        also recomputes deployment start times since node is shut down and
        restarted between main test sections.
        '''
        # write a custom forks.csv file containing test deployments.
        # blocks are 1 second apart by default in this regtest
        fork_csv_filename = os.path.join(self.options.tmpdir, "forks.csv")
        # forks.csv fields:
        # network,bit,name,starttime,timeout,windowsize,threshold,minlockedblocks,minlockedtime,gbtforce
        with open(fork_csv_filename, 'wt') as fh:
            # use current time to compute offset starttimes
            self.init_time = int(time.time())
            # starttimes are offset by 30 seconds from init_time
            self.fork_starttime = self.init_time + 1
            fh.write(
                "# deployment info for network 'regtest':\n" +

                ########## THRESHOLD TESTING BITS (1-6) ############

                # bit 0: test 'csv' fork renaming/reparameterization
                "regtest,0,bip135test0,%d,999999999999,144,108,0,0,true\n" % (self.fork_starttime) +

                # bit 1: test 'segwit' fork renaming/reparameterization
                "regtest,1,bip135test1,%d,999999999999,144,108,0,0,true\n" % (self.fork_starttime) +

                # bit 2: test minimum threshold
                "regtest,2,bip135test2,%d,999999999999,100,1,0,0,true\n" % (self.fork_starttime) +

                # bit 3: small threshold
                "regtest,3,bip135test3,%d,999999999999,100,10,0,0,true\n" % (self.fork_starttime) +

                # bit 4: supermajority threshold
                "regtest,4,bip135test4,%d,999999999999,100,75,0,0,true\n" % (self.fork_starttime) +

                # bit 5: high threshold
                "regtest,5,bip135test5,%d,999999999999,100,95,0,0,true\n" % (self.fork_starttime) +

                # bit 6: max-but-one threshold
                "regtest,6,bip135test6,%d,999999999999,100,99,0,0,true\n" % (self.fork_starttime) +

                # bit 7: max threshold
                "regtest,7,bip135test7,%d,999999999999,100,100,0,0,true\n" % (self.fork_starttime)

            )

        self.nodes = start_nodes(1, self.options.tmpdir,
                                 extra_args=[['-debug=all', '-forks=%s' % fork_csv_filename]])

    def run_test(self):
        time.sleep(5)  # So defined forks activate
        node = self.nodes[0]
        node.generate(100)

        try:  # expect exception bad value
            node.set("mining.vote=foo")
            assert(0)
        except JSONRPCException as e:
            pass

        node.set("mining.unsafeGetBlockTemplate=true")

        # test that initial configuration works
        cand = node.getminingcandidate()
        assert_equal(cand["version"], 0x20000080)

        # test that various setting are properly shown in the version of requested block candidates
        node.set("mining.vote=bip135test0")
        cand = node.getminingcandidate()
        assert_equal(cand["version"], 0x20000001)
        node.set("mining.vote= bip135test2 , bip135test1")
        cand = node.getminingcandidate()
        assert_equal(cand["version"], 0x20000006)
        node.set("mining.vote=bip135test6,bip135test5,bip135test6")
        cand = node.getminingcandidate()
        assert_equal(cand["version"], 0x20000060)
        node.set("mining.vote=bip135test3")
        cand = node.getblocktemplate()
        assert_equal(cand["version"], 0x20000008)
        node.set("mining.vote=")
        cand = node.getblocktemplate()
        assert_equal(cand["version"], 0x20000000)


if __name__ == '__main__':
    BIP135VoteTest().main(bitcoinConfDict={"mining.vote":"bip135test7"})


# Create a convenient function for an interactive python debugging session
def Test():
    t = BIP135VoteTest()
    bitcoinConf = {
        "debug": ["blk", "mempool", "net", "req"],
        "mining.vote":"bip135test7"
    }
    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
