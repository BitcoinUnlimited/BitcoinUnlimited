#!/usr/bin/env python3
# Copyright (c) 2015-2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import time
import sys
if sys.version_info[0] < 3:
    raise "Use Python 3"
import logging
logging.basicConfig(format='%(asctime)s.%(levelname)s: %(message)s', level=logging.INFO, stream=sys.stdout)

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
from test_framework.nodemessages import *
from test_framework.script import *


class MyTest (BitcoinTestFramework):

    def setup_chain(self, bitcoinConfDict=None, wallets=None):
        logging.info("Initializing test directory " + self.options.tmpdir)
        # pick this one to start from the cached 4 node 100 blocks mined configuration
        # initialize_chain(self.options.tmpdir)
        # pick this one to start at 0 mined blocks
        initialize_chain_clean(self.options.tmpdir, 4, bitcoinConfDict, wallets)

    def setup_network(self, split=False):
        self.nodes = start_nodes(2, self.options.tmpdir)
        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):

        node = self.nodes[0]

        node.generate(100)
        # generate enough blocks so that nodes[0] has a balance

        # test basic failure
        c = node.getminingcandidate()
        del c["merkleProof"]
        del c["prevhash"]
        id = c["id"]
        c["id"] = 100000  # bad ID
        ret = node.submitminingsolution(c)
        assert ret == "id not found"

        # didn't provide a nonce
        c = node.getminingcandidate()
        del c["merkleProof"]
        del c["prevhash"]
        expectException(lambda: node.submitminingsolution(c), JSONRPCException)

        # ask for a valid coinbase size
        c = node.getminingcandidate(1050)
		
        # ask for a coinbase size that is too big
        expectException(lambda: node.getminingcandidate(1000000000000000), JSONRPCException)
               
        # ask for a coinbase size that is too small
        expectException(lambda: node.getminingcandidate(-1), JSONRPCException)
        
        # the most awful mining algorithm: just submit with an arbitrary nonce
        # (works because testnet accepts almost anything)
        nonce = 0
        c["id"] = id
        while 1:
            nonce += 1
            c = node.getminingcandidate()
            del c["merkleProof"]
            del c["prevhash"]
            c["nonce"] = nonce
            ret = node.submitminingsolution(c)
            if ret is None:
                break

        assert_equal(101, node.getblockcount())

        # change the time and version and ensure that the block contains that result
        nonce = 0
        c["id"] = id
        while 1:
            nonce += 1
            c = node.getminingcandidate()
            del c["merkleProof"]
            del c["prevhash"]
            del c["nBits"]
            chosentime = c["time"] = c["time"] + 1
            c["nonce"] = nonce
            c["version"] = 0x123456
            ret = node.submitminingsolution(c)
            if ret is None:
                break

        assert_equal(102, node.getblockcount())
        block = node.getblock(node.getbestblockhash())
        assert_equal(chosentime, block["time"])
        assert_equal(0x123456, block["version"])

        # change the coinbase
        tx = CTransaction()
        tx.deserialize(BytesIO(unhexlify(c["coinbase"])))
        tx.vout[0].scriptPubKey = CScript([OP_1])

        nonce = 0
        c["id"] = id
        while 1:
            nonce += 1
            c = node.getminingcandidate()
            del c["merkleProof"]
            del c["prevhash"]
            del c["nBits"]
            c["nonce"] = nonce
            c["coinbase"] = hexlify(tx.serialize()).decode()
            ret = node.submitminingsolution(c)
            if ret is None:
                break

        assert_equal(103, node.getblockcount())
        blockhex = node.getblock(node.getbestblockhash(), False)
        block = CBlock()
        block.deserialize(BytesIO(unhexlify(blockhex)))
        assert_equal(block.vtx[0].vout[0].scriptPubKey, CScript([OP_1]))


if __name__ == '__main__':
    MyTest().main()

# Create a convenient function for an interactive python debugging session


def Test():
    t = MyTest()
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000  # we don't want any transactions rejected due to insufficient fees...
    }

    flags = []
    # you may want these additional flags:
    # flags.append("--nocleanup")
    # flags.append("--noshutdown")

    # Execution is much faster if a ramdisk is used, so use it if one exists in a typical location
    if os.path.isdir("/ramdisk/test"):
        flags.append("--tmppfx=/ramdisk/test")

    # Out-of-source builds are awkward to start because they need an additional flag
    # automatically add this flag during testing for common out-of-source locations
    here = os.path.dirname(os.path.abspath(__file__))
    if not os.path.exists(os.path.abspath(here + "/../../src/bitcoind")):
        dbg = os.path.abspath(here + "/../../debug/src/bitcoind")
        rel = os.path.abspath(here + "/../../release/src/bitcoind")
        if os.path.exists(dbg):
            print("Running from the debug directory (%s)" % dbg)
            flags.append("--srcdir=%s" % os.path.dirname(dbg))
        elif os.path.exists(rel):
            print("Running from the release directory (%s)" % rel)
            flags.append("--srcdir=%s" % os.path.dirname(rel))

    t.main(flags, bitcoinConf, None)
