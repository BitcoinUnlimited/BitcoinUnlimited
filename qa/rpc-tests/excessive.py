#!/usr/bin/env python3
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
# Test emergent consensus scenarios

import time
import random
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal
from test_framework.util import *
from test_framework.blocktools import *
import test_framework.script as script
import pdb
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging


def mostly_sync_mempools(rpc_connections, difference=50, wait=1, verbose=1):
    """
    Wait until everybody has the most of the same transactions in their memory
    pools. There is no guarantee that mempools will ever sync due to the
    filterInventoryKnown bloom filter.
    """
    iterations = 0
    while True:
        iterations += 1
        pool = set(rpc_connections[0].getrawmempool())
        num_match = 1
        poolLen = [len(pool)]
        for i in range(1, len(rpc_connections)):
            tmp = set(rpc_connections[i].getrawmempool())
            if tmp == pool:
                num_match = num_match + 1
            if iterations > 10 and len(tmp.symmetric_difference(pool)) < difference:
                num_match = num_match + 1
            poolLen.append(len(tmp))
        if verbose:
            logging.info("sync mempool: " + str(poolLen))
        if num_match == len(rpc_connections):
            break
        time.sleep(wait)


class ExcessiveBlockTest (BitcoinTestFramework):
    def __init__(self, extended=False):
        self.extended = extended
        BitcoinTestFramework.__init__(self)

    def setup_network(self, split=False):
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir, ["-debug=net", "-debug=graphene", "-usecashaddr=0", "-rpcservertimeout=0"], timewait=60 * 10))
        self.nodes.append(start_node(1, self.options.tmpdir, ["-debug=net", "-debug=graphene", "-usecashaddr=0", "-rpcservertimeout=0"], timewait=60 * 10))
        self.nodes.append(start_node(2, self.options.tmpdir, ["-debug=net", "-debug=graphene", "-usecashaddr=0", "-rpcservertimeout=0"], timewait=60 * 10))
        self.nodes.append(start_node(3, self.options.tmpdir, ["-debug=net", "-debug=graphene", "-usecashaddr=0", "-rpcservertimeout=0"], timewait=60 * 10))

        interconnect_nodes(self.nodes)
        self.is_network_split = False
        self.sync_all()

        if 0:  # getnewaddress can be painfully slow.  This bit of code can be used to during development to
               # create a wallet with lots of addresses, which then can be used in subsequent runs of the test.
               # It is left here for developers to manually enable.
            TEST_SIZE = 100  # TMP 00
            print("Creating addresses...")
            self.nodes[0].keypoolrefill(TEST_SIZE + 1)
            addrs = [self.nodes[0].getnewaddress() for _ in range(TEST_SIZE + 1)]
            with open("walletAddrs.json", "w") as f:
                f.write(str(addrs))
                pdb.set_trace()

    def run_test(self):
        BitcoinTestFramework.run_test(self)
        self.testCli()

        # clear out the mempool
        for n in self.nodes:
            while len(n.getrawmempool()):
                n.generate(1)
                sync_blocks(self.nodes)
        logging.info("cleared mempool: %s" % str([len(x) for x in [y.getrawmempool() for y in self.nodes]]))
        self.testExcessiveBlockSize()

    def testCli(self):

        # Assumes the default excessive at 32MB and mining at 8MB
        try:
            self.nodes[0].setminingmaxblock(33000000)
        except JSONRPCException as e:
            pass
        else:
            assert(0)  # was able to set the mining size > the excessive size

        try:
            self.nodes[0].setminingmaxblock(99)
        except JSONRPCException as e:
            pass
        else:
            assert(0)  # was able to set the mining size below our arbitrary minimum

        try:
            self.nodes[0].setexcessiveblock(1000, 10)
        except JSONRPCException as e:
            pass
        else:
            assert(0)  # was able to set the excessive size < the mining size

    def sync_all(self):
        """Synchronizes blocks and mempools (mempools may never fully sync)"""
        if self.is_network_split:
            sync_blocks(self.nodes[:2])
            sync_blocks(self.nodes[2:])
            mostly_sync_mempools(self.nodes[:2])
            mostly_sync_mempools(self.nodes[2:])
        else:
            sync_blocks(self.nodes)
            mostly_sync_mempools(self.nodes)

    def expectHeights(self, blockHeights, waittime=10):
        loop = 0
        count = []
        while loop < waittime:
            counts = [x.getblockcount() for x in self.nodes]
            if counts == blockHeights:
                return True  # success!
            else:
                for (a,b) in zip(counts, blockHeights):
                    if counts > blockHeights:
                        assert("blockchain synced too far")
            time.sleep(.25)
            loop += .25
            if int(loop) == loop and (int(loop) % 10) == 0:
                logging.info("...waiting %f %s != %s" % (loop, counts, blockHeights))
        return False

    def repeatTx(self, count, node, addr, amt=1.0):
        for i in range(0, count):
            node.sendtoaddress(addr, amt)

    def generateAndPrintBlock(self, node):
        hsh = node.generate(1)
        inf = node.getblock(hsh[0])
        logging.info("block %d size %d" % (inf["height"], inf["size"]))
        return hsh

    def testExcessiveBlockSize(self):

        # get spendable coins
        if 0:
            for n in self.nodes:
                n.generate(1)
                self.sync_all()
            self.nodes[0].generate(100)

        # Set the accept depth at 1, 2, and 3 and watch each nodes resist the chain for that long
        self.nodes[0].setminingmaxblock(5000)  # keep the generated blocks within 16*the EB so no disconnects
        self.nodes[1].setminingmaxblock(1000)
        self.nodes[2].setminingmaxblock(1000)
        self.nodes[3].setminingmaxblock(1000)

        self.nodes[1].setexcessiveblock(1000, 1)
        self.nodes[2].setexcessiveblock(1000, 2)
        self.nodes[3].setexcessiveblock(1000, 3)

        logging.info("Test excessively sized block, not propagating until accept depth is exceeded")
        addr = self.nodes[3].getnewaddress()
        # By using a very small value, it is likely that a single input is used.  This is important because
        # our mined block size is so small in this test that if multiple inputs are used the transactions
        # might not fit in the block.  This will give us a short block when the test expects a larger one.
        # To catch any of these short-block test malfunctions, the block size is printed out.
        self.repeatTx(8, self.nodes[0], addr, .001)
        counts = [x.getblockcount() for x in self.nodes]
        base = counts[0]
        logging.info("Starting counts: %s" % str(counts))
        logging.info("node0")
        self.generateAndPrintBlock(self.nodes[0])
        assert_equal(True, self.expectHeights([base + 1, base, base, base]))

        logging.info("node1")
        self.nodes[0].generate(1)
        assert_equal(True, self.expectHeights([base + 2, base + 2, base, base]))

        logging.info("node2")
        self.nodes[0].generate(1)
        assert_equal(True, self.expectHeights([base + 3, base + 3, base + 3, base]))

        logging.info("node3")
        self.nodes[0].generate(1)
        assert_equal(True, self.expectHeights([base + 4] * 4))

        # Now generate another excessive block, but all nodes should snap right to
        # it because they have an older excessive block
        logging.info("Test immediate propagation of additional excessively sized block, due to prior excessive")
        self.repeatTx(8, self.nodes[0], addr, .001)
        self.nodes[0].generate(1)
        assert_equal(True, self.expectHeights([base + 5] * 4))

        logging.info("Test daily excessive reset")
        # Now generate a day's worth of small blocks which should re-enable the
        # node's reluctance to accept a large block
        self.nodes[0].generate(6 * 24)
        sync_blocks(self.nodes)
        self.nodes[0].generate(5)  # plus the accept depths
        sync_blocks(self.nodes)
        self.repeatTx(8, self.nodes[0], addr, .001)
        base = self.nodes[0].getblockcount()
        self.generateAndPrintBlock(self.nodes[0])
        time.sleep(2)  # give blocks a chance to fully propagate
        counts = [x.getblockcount() for x in self.nodes]
        assert_equal(counts, [base + 1, base, base, base])

        self.repeatTx(8, self.nodes[0], addr, .001)
        self.generateAndPrintBlock(self.nodes[0])
        time.sleep(2)  # give blocks a chance to fully propagate
        sync_blocks(self.nodes[0:2])
        counts = [x.getblockcount() for x in self.nodes]
        assert_equal(counts, [base + 2, base + 2, base, base])

        self.repeatTx(5, self.nodes[0], addr, .001)
        self.generateAndPrintBlock(self.nodes[0])
        time.sleep(2)  # give blocks a chance to fully propagate
        sync_blocks(self.nodes[0:3])
        counts = [x.getblockcount() for x in self.nodes]
        assert_equal(counts, [base + 3, base + 3, base + 3, base])

        self.repeatTx(5, self.nodes[0], addr, .001)
        self.generateAndPrintBlock(self.nodes[0])
        sync_blocks(self.nodes)
        counts = [x.getblockcount() for x in self.nodes]
        assert_equal(counts, [base + 4] * 4)

        self.repeatTx(5, self.nodes[0], addr, .001)
        self.generateAndPrintBlock(self.nodes[0])
        sync_blocks(self.nodes)
        counts = [x.getblockcount() for x in self.nodes]
        assert_equal(counts, [base + 5] * 4)

        if self.extended:
            logging.info("Test daily excessive reset #2")
            # Now generate a day's worth of small blocks which should re-enable the
            # node's reluctance to accept a large block + 10 because we have to get
            # beyond all the node's accept depths
            self.nodes[0].generate(6 * 24 + 10)
            sync_blocks(self.nodes)

            # counts = [ x.getblockcount() for x in self.nodes ]
            self.nodes[1].setexcessiveblock(100000, 1)  # not sure how big the txns will be but smaller than this
            self.nodes[1].setminingmaxblock(100000)  # not sure how big the txns will be but smaller than this
            self.repeatTx(20, self.nodes[0], addr, .001)
            base = self.nodes[0].getblockcount()
            self.generateAndPrintBlock(self.nodes[0])
            time.sleep(2)  # give blocks a chance to fully propagate
            sync_blocks(self.nodes[0:2])
            counts = [x.getblockcount() for x in self.nodes]
            assert_equal(counts, [base + 1, base + 1, base, base])

        if self.extended:
            logging.info("Random test")
            randomRange = 3
        else:
            randomRange = 0

        for i in range(0, randomRange):
            logging.info("round %d" % i)
            for n in self.nodes:
                size = random.randint(1, 1000) * 1000
                try:  # since miningmaxblock must be <= excessiveblock, raising/lowering may need to run these in different order
                    n.setminingmaxblock(size)
                    n.setexcessiveblock(size, random.randint(0, 10))
                except JSONRPCException:
                    n.setexcessiveblock(size, random.randint(0, 10))
                    n.setminingmaxblock(size)

            addrs = [x.getnewaddress() for x in self.nodes]
            ntxs = 0
            for i in range(0, random.randint(1, 20)):
                try:
                    n = random.randint(0, 3)
                    logging.info("%s: Send to %d" % (ntxs, n))
                    self.nodes[n].sendtoaddress(addrs[random.randint(0, 3)], .1)
                    ntxs += 1
                except JSONRPCException:  # could be spent all the txouts
                    pass
            logging.info("%d transactions" % ntxs)
            time.sleep(1)  # allow txns a chance to propagate
            self.nodes[random.randint(0, 3)].generate(1)
            logging.info("mined a block")
            # TODO:  rather than sleeping we should really be putting a check in here
            #       based on what the random excessive seletions were from above
            time.sleep(5)  # allow block a chance to propagate

        # the random test can cause disconnects if the block size is very large compared to excessive size
        # so reconnect
        interconnect_nodes(self.nodes)


if __name__ == '__main__':

    if "--extensive" in sys.argv:
        longTest = True
        # we must remove duplicate 'extensive' arg here
        while True:
            try:
                sys.argv.remove('--extensive')
            except:
                break
        logging.info("Running extensive tests")
    else:
        longTest = False

    ExcessiveBlockTest(longTest).main()


def info(type, value, tb):
    if hasattr(sys, 'ps1') or not sys.stderr.isatty():
        # we are in interactive mode or we don't have a tty-like
        # device, so we call the default hook
        sys.__excepthook__(type, value, tb)
    else:
        import traceback
        import pdb
        # we are NOT in interactive mode, print the exception...
        traceback.print_exception(type, value, tb)
        print
        # ...then start the debugger in post-mortem mode.
        pdb.pm()


sys.excepthook = info

def Test():
    t = ExcessiveBlockTest()
    t.drop_to_pdb = True
    bitcoinConf = {
        "debug": ["rpc", "net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000,  # we don't want any transactions rejected due to insufficient fees...
        "blockminsize": 1000000
    }

    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
