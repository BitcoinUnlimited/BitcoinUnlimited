#!/usr/bin/env python3
# Copyright (c) 2022 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test the wallet sending to p2sh32 addresses.  This should fail as non-standard pre-activation, and work ok
post-activation"""
import time

from test_framework import cashaddr
from test_framework.key import CECKey
from test_framework.nodemessages import FromHex, CTransaction, wait_until
from test_framework.script import (
    CScript,
    OP_CHECKSIG, OP_DUP, OP_EQUAL, OP_EQUALVERIFY, OP_HASH160, OP_HASH256,
)
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, connect_nodes_bi, hash160, hash256, assert_raises_rpc_error
import logging


class WalletP2SH32Test(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 2
        # node 0 does not accept non-std txns, node 1 does accept non-std txns
        self.extra_args = [['-upgrade9activationtime=999999999999', '-acceptnonstdtxn=0',
                            '-whitelist=127.0.0.1', ],
                           ['-upgrade9activationtime=999999999999', '-acceptnonstdtxn=1',
                            '-whitelist=127.0.0.1', ]]
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        logging.info("Mining blocks...")

        addr0 = self.nodes[0].getnewaddress()
        addr1 = self.nodes[1].getnewaddress()

        # Ensure both node0 and node1 have funds
        self.nodes[0].generatetoaddress(10, addr0)
        self.sync_all()
        self.nodes[1].generatetoaddress(10, addr1)
        self.sync_all()
        # Mature the above 2 sets of coins
        self.nodes[0].generatetoaddress(51, addr0)
        self.sync_all()
        self.nodes[1].generatetoaddress(50, addr1)
        self.sync_all()

        priv_key = CECKey()
        privkey_bytes = b'WalletP2SH32_WalletP2SH32_Wallet'
        priv_key.set_secretbytes(privkey_bytes)
        # Make p2sh32 wrapping p2pkh
        pub_key = priv_key.get_pubkey()
        redeem_script = CScript([OP_DUP, OP_HASH160, hash160(pub_key), OP_EQUALVERIFY, OP_CHECKSIG])
        # scriptPubKey for p2sh32
        spk_p2sh32 = CScript([OP_HASH256, hash256(redeem_script), OP_EQUAL])
        addr_p2sh32 = cashaddr.encode("bchreg", cashaddr.SCRIPT_TYPE, hash256(redeem_script))

        logging.info(f"Sending to p2sh32 {addr_p2sh32} via node0 ...")
        # First, send funds to a p2sh_32.. this should fail as non-standard
        assert_raises_rpc_error(-4, None, self.nodes[0].sendtoaddress, addr_p2sh32, 1.0)

        logging.info(f"Sending to p2sh32 {addr_p2sh32} via node1 ...")
        # This one accepts non-std so it should succeed
        txid = self.nodes[1].sendtoaddress(addr_p2sh32, 1.0)
        tx = FromHex(CTransaction(), self.nodes[1].gettransaction(txid)["hex"])
        assert_equal(tx.vout[0].scriptPubKey.hex(), spk_p2sh32.hex())  # Ensure addr_p2sh32 parsed ok
        assert txid in self.nodes[1].getrawmempool()
        logging.info(f"txid: {txid} in mempool")
        time.sleep(1.0)  # Give txn some time to propagate for below check
        assert txid not in self.nodes[0].getrawmempool()

        # Confirm the mempool for node1
        self.nodes[1].generatetoaddress(1, addr1)
        self.sync_all()

        # Activate Upgrade9
        logging.info("Activating Upgrade9 ...")

        # Get the current MTP time
        activation_time = self.nodes[0].getblockchaininfo()["mediantime"]
        for node_num, args in enumerate(self.extra_args):
            args = args.copy()
            assert_equal(args[0], '-upgrade9activationtime=999999999999')
            args[0] = f"-upgrade9activationtime={activation_time}"
            self.restart_node(node_num, extra_args=args)
        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_all()

        logging.info(f"Sending to p2sh32 {addr_p2sh32} via node0 ...")
        txid = self.nodes[0].sendtoaddress(addr_p2sh32, 1.0)
        tx = FromHex(CTransaction(), self.nodes[0].gettransaction(txid)["hex"])
        assert_equal(tx.vout[0].scriptPubKey.hex(), spk_p2sh32.hex())  # Ensure addr_p2sh32 parsed ok
        assert txid in self.nodes[0].getrawmempool()
        self.sync_all()
        assert txid in self.nodes[1].getrawmempool()
        logging.info(f"txid: {txid} in mempool")

        logging.info(f"Sending to p2sh32 {addr_p2sh32} via node1 ...")
        txid = self.nodes[1].sendtoaddress(addr_p2sh32, 1.0)
        tx = FromHex(CTransaction(), self.nodes[1].gettransaction(txid)["hex"])
        assert_equal(tx.vout[0].scriptPubKey.hex(), spk_p2sh32.hex())  # Ensure addr_p2sh32 parsed ok
        assert txid in self.nodes[1].getrawmempool()
        self.sync_all()
        assert txid in self.nodes[0].getrawmempool()
        logging.info(f"txid: {txid} in mempool")

        # Ensure that wallet supports managing p2sh_32 addresses as watching-only
        # If bitcoind is compiled with debug, this will fail, see issue #2260
        bal = self.nodes[0].getbalance()
        self.nodes[0].importaddress(addr_p2sh32, "My P2SH32 Watching-Only", True)
        wait_until(lambda: self.nodes[0].getbalance("*", 0, True) > bal)


if __name__ == '__main__':
    WalletP2SH32Test().main()
