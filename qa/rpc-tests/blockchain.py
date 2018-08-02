#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import test_framework.loginit
#
# Test RPC calls related to blockchain state. Tests correspond to code in
# rpc/blockchain.cpp.
#

from decimal import Decimal

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import *


class BlockchainTest(BitcoinTestFramework):
    """
    Test blockchain-related RPC calls:

        - gettxoutsetinfo
        - verifychain

    """

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain(self.options.tmpdir)

    def setup_network(self, split=False):
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir, ["-debug=net"]))
        self.nodes.append(start_node(1, self.options.tmpdir, ["-debug=net"]))
        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        self._test_gettxoutsetinfo()
        self._test_getblockheader()
        self._test_rollbackchain()
        self._test_transaction_pools()
        self.nodes[0].verifychain(4, 0)

    def _test_gettxoutsetinfo(self):
        node = self.nodes[0]
        res = node.gettxoutsetinfo()

        assert_equal(res['total_amount'], Decimal('8725.00000000'))
        assert_equal(res['transactions'], 200)
        assert_equal(res['height'], 200)
        assert_equal(res['txouts'], 200)
        size = res["disk_size"]
        assert (size > 6400)
        assert (size < 64000)
        assert_equal(res['bestblock'], node.getblockhash(200))
        assert_equal(len(res['bestblock']), 64)
        assert_equal(len(res['hash_serialized_2']), 64)

        print ("Test that gettxoutsetinfo() works for blockchain with just the genesis block")
        b1hash = node.getblockhash(1)
        node.invalidateblock(b1hash)

        res2 = node.gettxoutsetinfo()
        assert_equal(res2['transactions'], 0)
        assert_equal(res2['total_amount'], Decimal('0'))
        assert_equal(res2['height'], 0)
        assert_equal(res2['txouts'], 0)
        assert_equal(res2['bestblock'], node.getblockhash(0))
        assert_equal(len(res2['hash_serialized_2']), 64)

        print ("Test that gettxoutsetinfo() returns the same result after invalidate/reconsider block")
        node.reconsiderblock(b1hash)

        res3 = node.gettxoutsetinfo()
        assert_equal(res['total_amount'], res3['total_amount'])
        assert_equal(res['transactions'], res3['transactions'])
        assert_equal(res['height'], res3['height'])
        assert_equal(res['txouts'], res3['txouts'])
        assert_equal(res['bestblock'], res3['bestblock'])
        assert_equal(res['hash_serialized_2'], res3['hash_serialized_2'])

    def _test_getblockheader(self):
        node = self.nodes[0]

        assert_raises(
            JSONRPCException, lambda: node.getblockheader('nonsense'))

        besthash = node.getbestblockhash()
        secondbesthash = node.getblockhash(199)
        header = node.getblockheader(besthash)

        assert_equal(header['hash'], besthash)
        assert_equal(header['height'], 200)
        assert_equal(header['confirmations'], 1)
        assert_equal(header['previousblockhash'], secondbesthash)
        assert_is_hex_string(header['chainwork'])
        assert_is_hash_string(header['hash'])
        assert_is_hash_string(header['previousblockhash'])
        assert_is_hash_string(header['merkleroot'])
        assert_is_hash_string(header['bits'], length=None)
        assert isinstance(header['time'], int)
        assert isinstance(header['mediantime'], int)
        assert isinstance(header['nonce'], int)
        assert isinstance(header['version'], int)
        assert isinstance(int(header['versionHex'], 16), int)
        assert isinstance(header['difficulty'], Decimal)

    def _test_rollbackchain(self):
        # Save the hash of the current chaintip and then mine 10 blocks
        blockcount = self.nodes[0].getblockcount()

        self.nodes[0].generate(10)
        self.sync_all()
        assert_equal(blockcount + 10, self.nodes[0].getblockcount())
        assert_equal(blockcount + 10, self.nodes[1].getblockcount())

        # Now Rollback the chain on Node 0 by 5 blocks
        print ("Test that rollbackchain() works")
        blockcount = self.nodes[0].getblockcount()
        self.nodes[0].rollbackchain(self.nodes[0].getblockcount() - 5)
        assert_equal(blockcount - 5, self.nodes[0].getblockcount())
        assert_equal(blockcount, self.nodes[1].getblockcount())

        # Invalidate the chaintip on Node 0 and then mine more blocks on Node 1
        # - Node1 should advance in chain length but Node 0 shoudd not follow.
        self.nodes[1].generate(5)
        time.sleep(2) # give node0 a chance to sync (it shouldn't)

        assert_equal(self.nodes[0].getblockcount() + 10, self.nodes[1].getblockcount())
        assert_not_equal(self.nodes[0].getbestblockhash(), self.nodes[1].getbestblockhash())

        # Now mine blocks on node0 which will extend the chain beyond node1.
        self.nodes[0].generate(12)

        # Reconnect nodes since they will have been disconnected when nod0's chain was previously invalidated.
        # -  Node1 should re-org and follow node0's chain.
        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_all()
        assert_equal(self.nodes[0].getblockcount(), self.nodes[1].getblockcount())
        assert_equal(self.nodes[0].getbestblockhash(), self.nodes[1].getbestblockhash())


        # Test that we can only rollback the chain by max 100 blocks
        self.nodes[0].generate(100)
        self.sync_all()

        # Roll back by 101 blocks, this should fail
        blockcount = self.nodes[0].getblockcount()
        try:
            self.nodes[0].rollbackchain(self.nodes[0].getblockcount() - 101)
        except JSONRPCException as e:
            print (e.error['message'])
            assert("You are attempting to rollback the chain by 101 blocks, however the limit is 100 blocks." in e.error['message'])
        assert_equal(blockcount, self.nodes[0].getblockcount())
        assert_equal(blockcount, self.nodes[1].getblockcount())

        # Now rollback by 100 blocks
        bestblockhash = self.nodes[0].getbestblockhash() #save for later
        blockcount = self.nodes[0].getblockcount()
        self.nodes[0].rollbackchain(self.nodes[0].getblockcount() - 100)
        assert_equal(blockcount - 100, self.nodes[0].getblockcount())
        assert_equal(blockcount, self.nodes[1].getblockcount())

        # Now reconsider the now invalid chaintip on node0 which will reconnect the blocks
        self.nodes[0].reconsiderblock(bestblockhash)
        self.sync_all()

        # Now rollback by 101 blocks by using the override
        bestblockhash = self.nodes[0].getbestblockhash() #save for later
        blockcount = self.nodes[0].getblockcount()
        self.nodes[0].rollbackchain(self.nodes[0].getblockcount() - 101, True)
        assert_equal(blockcount - 101, self.nodes[0].getblockcount())
        assert_equal(blockcount, self.nodes[1].getblockcount())

        # Now reconsider the now invalid chaintip on node0 which will reconnect the blocks
        self.nodes[0].reconsiderblock(bestblockhash)
        self.sync_all()

        ### Test that we can rollback the chain beyond a forkpoint and then reconnect
        #   the blocks on either chain

        # Mine a few blocks
        self.nodes[0].generate(50)

        # Invalidate the chaintip and then mine another chain
        bestblockhash1 = self.nodes[0].getbestblockhash() #save for later
        self.nodes[0].invalidateblock(bestblockhash1)
        self.nodes[0].generate(5)

        # Reconsider the previous chain so both chains are either valid or fork-active.
        self.nodes[0].reconsiderblock(bestblockhash1)

        # Invalidate the current longer fork2 and mine 10 blocks on fork1
        # which now makes it the longer fork
        bestblockhash2 = self.nodes[0].getbestblockhash() #save for later
        self.nodes[0].invalidateblock(bestblockhash2)
        self.nodes[0].generate(10)

        # Reconsider fork2 so both chains are active.
        # fork1 should be 10 blocks long and fork 2 should be 5 blocks long with fork1 being active
        # and fork2 being fork-valid.
        self.nodes[0].reconsiderblock(bestblockhash2)

        # Now we're ready to test the rollback. Rollback beyond the fork point (more than 10 blocks).
        self.nodes[0].rollbackchain(self.nodes[0].getblockcount() - 20)

        # Reconsider the fork1. Blocks should now be fully reconnected on fork1.
        self.nodes[0].reconsiderblock(bestblockhash1)
        assert_equal(self.nodes[0].getbestblockhash(), bestblockhash1);

        # Rollback again beyond the fork point (more than 10 blocks).
        self.nodes[0].rollbackchain(self.nodes[0].getblockcount() - 20)

        # Reconsider the fork2. Blocks should now be fully reconnected on fork2.
        self.nodes[0].reconsiderblock(bestblockhash2)
        assert_equal(self.nodes[0].getbestblockhash(), bestblockhash2);

    def _test_transaction_pools(self):
        node = self.nodes[0]

        # main txn pool
        res = node.getmempoolinfo()
        assert_equal(res['size'], 0)
        assert_equal(res['bytes'], 0)
        assert_equal(res['usage'], 0)
        assert_equal(res['bytes'], 0)
        assert_equal(res['maxmempool'], 300000000)
        assert_equal(res['mempoolminfee'], Decimal('0E-8'))

        # orphan pool
        res2 = node.getorphanpoolinfo()
        assert_equal(res2['size'], 0)
        assert_equal(res2['bytes'], 0)


if __name__ == '__main__':
    BlockchainTest().main()
