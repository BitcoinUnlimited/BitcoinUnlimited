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


class MiningTest (BitcoinTestFramework):

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
        sync_blocks(self.nodes)

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
        tx = CTransaction().deserialize(c["coinbase"])
        tx.vout[0].scriptPubKey = CScript(([OP_NOP] * 50) + [OP_1])  # 50 no-ops because tx must be 100 bytes or more
        nonce = 0
        c["id"] = id
        while 1:
            nonce += 1
            if (nonce&127)==0:
                logging.info("simple mining nonce: " + str(nonce))
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
        assert_equal(block.vtx[0].vout[0].scriptPubKey, CScript(([OP_NOP] * 50) + [OP_1]))

        #### Test that a dynamic relay policy change does not effect the mining
        #    of txns currently in the mempool.
        self.nodes[0].generate(5);
        self.sync_all()

        # Add a few txns to the mempool and mine them with the default fee
        self.nodes[0].set("minlimitertxfee=0")
        self.nodes[1].set("minlimitertxfee=0")
        self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 1)
        self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 1)
        self.sync_all()
        assert_equal(str(self.nodes[0].getnetworkinfo()["relayfee"]), "0E-8")
        assert_equal(self.nodes[0].getmempoolinfo()["size"], 2)
        assert_equal(self.nodes[0].getmempoolinfo()["mempoolminfee"], 0)
        self.nodes[0].generate(1);
        self.sync_all()
        assert_equal(self.nodes[0].getmempoolinfo()["size"], 0)
        assert_equal(self.nodes[0].getmempoolinfo()["mempoolminfee"], 0)

        # Add a few txns to the mempool, then increase the relayfee beyond what the txns would pay
        # and mine a block. All txns should be mined and removed from the
        self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 1)
        self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 1)
        self.sync_all()
        assert_equal(self.nodes[0].getmempoolinfo()["size"], 2)
        assert_equal(self.nodes[0].getmempoolinfo()["mempoolminfee"], 0)

        # Make the minlimitertxfee so high it would be higher than any possible fee.
        self.nodes[0].set("minlimitertxfee=1000")
        self.nodes[1].set("minlimitertxfee=1000")
        self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 1)
        self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 1)
        self.sync_all()
        assert_equal(str(self.nodes[0].getnetworkinfo()["relayfee"]), "0.01000000")
        self.nodes[0].generate(1)
        self.sync_all()
        assert_equal(self.nodes[0].getmempoolinfo()["size"], 0)


if __name__ == '__main__':
    MiningTest().main(None, {  "blockprioritysize": 2000000, "blockminsize":200000 })

# Create a convenient function for an interactive python debugging session


def Test():
    t = MiningTest()
    bitcoinConf = {
        "debug": ["net", "blk", "thin", "mempool", "req", "bench", "evict"],
        "blockprioritysize": 2000000,  # we don't want any transactions rejected due to insufficient fees...
        "blockminsize":200000
    }

    flags = standardFlags()
    t.main(flags, bitcoinConf, None)
