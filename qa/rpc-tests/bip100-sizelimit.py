#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test mining and broadcast of larger-than-1MB-blocks
#
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *

from decimal import Decimal

CACHE_DIR = "cache_bigblock"

class BigBlockTest(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)

        if not os.path.isdir(os.path.join(CACHE_DIR, "node0")):
            print("Creating initial chain. This will be cached for future runs.")

            for i in range(4):
                initialize_datadir(CACHE_DIR, i) # Overwrite port/rpcport in bitcoin.conf

            # Node 0 creates 8MB blocks that vote for increase to 8MB
            # Node 1 creates empty blocks that vote for 8MB
            # Node 2 creates empty blocks that vote for 2MB
            # Node 3 creates empty blocks that do not vote for increase
            self.nodes = []
            # Use node0 to mine blocks for input splitting
            self.nodes.append(start_node(0, CACHE_DIR, ["-blockmaxsize=8000000", "-bip100=1", "-maxblocksizevote=8", "-limitancestorsize=2000", "-limitdescendantsize=2000"]))
            self.nodes.append(start_node(1, CACHE_DIR, ["-blockmaxsize=1000", "-bip100=1", "-maxblocksizevote=8", "-limitancestorsize=2000", "-limitdescendantsize=2000"]))
            self.nodes.append(start_node(2, CACHE_DIR, ["-blockmaxsize=1000", "-bip100=1", "-maxblocksizevote=1", "-limitancestorsize=2000", "-limitdescendantsize=2000"]))
            self.nodes.append(start_node(3, CACHE_DIR, ["-blockmaxsize=1000", "-bip100=1", "-maxblocksizevote=2", "-limitancestorsize=2000", "-limitdescendantsize=2000"]))

            connect_nodes_bi(self.nodes, 0, 1)
            connect_nodes_bi(self.nodes, 1, 2)
            connect_nodes_bi(self.nodes, 2, 3)
            connect_nodes_bi(self.nodes, 3, 0)

            self.is_network_split = False

            # Create a 2012-block chain in a 75% ratio for increase (genesis block votes for 1MB)
            # Make sure they are not already sorted correctly
            blocks = []
            blocks.append(self.nodes[1].generate(503))
            assert(self.sync_blocks(self.nodes[1:3]))
            blocks.append(self.nodes[2].generate(502)) # <--- genesis is 503rd vote for 1MB
            assert(self.sync_blocks(self.nodes[2:4]))
            blocks.append(self.nodes[3].generate(503))
            assert(self.sync_blocks(self.nodes[1:4]))
            blocks.append(self.nodes[1].generate(503))
            assert(self.sync_blocks(self.nodes))

            tx_file = open(os.path.join(CACHE_DIR, "txdata"), "w")

            # Create a lot of tansaction data ready to be mined
            fee = Decimal('.00005')
            used = set()
            print("Creating transaction data")
            for i in range(0,25):
                inputs = []
                outputs = {}
                limit = 0
                utxos = self.nodes[3].listunspent(0)
                for utxo in utxos:
                    if not utxo["txid"]+str(utxo["vout"]) in used:
                        raw_input = {}
                        raw_input["txid"] = utxo["txid"]
                        raw_input["vout"] = utxo["vout"]
                        inputs.append(raw_input)
                        outputs[self.nodes[3].getnewaddress()] = utxo["amount"] - fee
                        used.add(utxo["txid"]+str(utxo["vout"]))
                        limit = limit + 1
                        if (limit >= 250):
                            break
                rawtx = self.nodes[3].createrawtransaction(inputs, outputs)
                txdata = self.nodes[3].signrawtransaction(rawtx)["hex"]
                self.nodes[3].sendrawtransaction(txdata)
                tx_file.write(txdata+"\n")
            tx_file.close()

            stop_nodes(self.nodes)
            wait_bitcoinds()
            self.nodes = []
            for i in range(4):
                os.remove(log_filename(CACHE_DIR, i, "db.log"))
                os.remove(log_filename(CACHE_DIR, i, "peers.dat"))
                os.remove(log_filename(CACHE_DIR, i, "fee_estimates.dat"))

        for i in range(4):
            from_dir = os.path.join(CACHE_DIR, "node"+str(i))
            to_dir = os.path.join(self.options.tmpdir,  "node"+str(i))
            shutil.copytree(from_dir, to_dir)
            initialize_datadir(self.options.tmpdir, i) # Overwrite port/rpcport in bitcoin.conf

    def sync_blocks(self, rpc_connections, wait=1, max_wait=60):
        """
        Wait until everybody has the same block count
        """
        for i in range(0,max_wait):
            if i > 0: time.sleep(wait)
            counts = [ x.getblockcount() for x in rpc_connections ]
            if counts == [ counts[0] ]*len(counts):
                return True
        return False

    def setup_network(self):
        self.nodes = []

        self.nodes.append(start_node(0, self.options.tmpdir, ["-blockmaxsize=8000000", "-bip100=1", "-maxblocksizevote=8", "-limitancestorsize=2000", "-limitdescendantsize=2000"], timewait=60))
        self.nodes.append(start_node(1, self.options.tmpdir, ["-blockmaxsize=1000", "-bip100=1", "-maxblocksizevote=8", "-limitancestorsize=2000", "-limitdescendantsize=2000"], timewait=60))
        self.nodes.append(start_node(2, self.options.tmpdir, ["-blockmaxsize=1000", "-bip100=1", "-maxblocksizevote=1", "-limitancestorsize=2000", "-limitdescendantsize=2000"], timewait=60))
        # (We don't restart the node with the huge wallet
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 0)

        self.load_mempool(self.nodes[0])

    def load_mempool(self, node):
        with open(os.path.join(CACHE_DIR, "txdata"), "r") as f:
            for line in f:
                node.sendrawtransaction(line.rstrip())

    def TestMineBig(self, expect_big):
        # Test if node0 will mine a block bigger than legacy MAX_BLOCK_SIZE
        self.nodes[0].setminingmaxblock(self.nodes[0].getexcessiveblock()["excessiveBlockSize"])
        b1hash = self.nodes[0].generate(1)[0]
        b1 = self.nodes[0].getblock(b1hash, True)
        assert(self.sync_blocks(self.nodes[0:3]))

        if expect_big:
            assert(b1['size'] > 1000*1000)

            # Have node1 mine on top of the block,
            # to make sure it goes along with the fork
            b2hash = self.nodes[1].generate(1)[0]
            b2 = self.nodes[1].getblock(b2hash, True)
            assert(b2['previousblockhash'] == b1hash)
            assert(self.sync_blocks(self.nodes[0:3]))

        else:
            assert(b1['size'] < 1000*1000)

        # Reset chain to before b1hash:
        for node in self.nodes[0:3]:
            node.invalidateblock(b1hash)
        assert(self.sync_blocks(self.nodes[0:3]))


    def run_test(self):
        # nodes 0 and 1 have mature 50-BTC coinbase transactions.

        print("Testing consensus blocksize increase conditions")

        assert_equal(self.nodes[0].getblockcount(), 2011) # This is a 0-based height

        # Current nMaxBlockSize is still 1MB
        assert_equal(self.nodes[0].getexcessiveblock()["excessiveBlockSize"], 1000000)
        self.TestMineBig(False)

        # Create a situation where the 1512th-highest vote is for 2MB
        self.nodes[2].generate(1)
        assert(self.sync_blocks(self.nodes[1:3]))
        ahash = self.nodes[1].generate(3)[2]
        assert_equal(self.nodes[1].getexcessiveblock()["excessiveBlockSize"], int(1000000 * 1.05))
        assert(self.sync_blocks(self.nodes[0:2]))
        self.TestMineBig(True)

        # Shutdown then restart node[0], it should produce a big block.
        stop_node(self.nodes[0], 0)
        self.nodes[0] = start_node(0, self.options.tmpdir, ["-blockmaxsize=8000000", "-bip100=1", "-maxblocksizevote=8", "-limitancestorsize=2000", "-limitdescendantsize=2000"], timewait=60)
        self.load_mempool(self.nodes[0])
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 2)
        assert_equal(self.nodes[0].getexcessiveblock()["excessiveBlockSize"], int(1000000 * 1.05))
        self.TestMineBig(True)

        # Test re-orgs past the sizechange block
        stop_node(self.nodes[0], 0)
        self.nodes[2].invalidateblock(ahash)
        assert_equal(self.nodes[2].getexcessiveblock()["excessiveBlockSize"], 1000000)
        self.nodes[2].generate(2)
        assert_equal(self.nodes[2].getexcessiveblock()["excessiveBlockSize"], 1000000)
        assert(self.sync_blocks(self.nodes[1:3]))

        # Restart node0, it should re-org onto longer chain,
        # and refuse to mine a big block:
        self.nodes[0] = start_node(0, self.options.tmpdir, ["-blockmaxsize=8000000", "-bip100=1", "-maxblocksizevote=8", "-limitancestorsize=2000", "-limitdescendantsize=2000"], timewait=60)
        self.load_mempool(self.nodes[0])
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 0, 2)
        assert(self.sync_blocks(self.nodes[0:3]))
        assert_equal(self.nodes[0].getexcessiveblock()["excessiveBlockSize"], 1000000)
        self.TestMineBig(False)

        # Mine 4 blocks voting for 8MB. Bigger block NOT ok, we are in the next voting period
        self.nodes[1].generate(4)
        assert_equal(self.nodes[1].getexcessiveblock()["excessiveBlockSize"], 1000000)
        assert(self.sync_blocks(self.nodes[0:3]))
        self.TestMineBig(False)


        print("Cached test chain and transactions left in %s"%(CACHE_DIR))

if __name__ == '__main__':
    BigBlockTest().main()
