#!/usr/bin/env python3
# Copyright (c) 2022 The Bitcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that the wallet can send to "token aware" CashAddresses (type=2 and type=3 addresses)."""
from decimal import Decimal

from test_framework import cashaddr
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_greater_than, assert_not_equal


class WalletSendToTokenAwareCashAddr(BitcoinTestFramework):

    def set_test_params(self):
        self.num_nodes = 2

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        assert_greater_than(self.nodes[0].getbalance(), 4.0)

        # Send 1 BCH normal
        node_1_balance = self.nodes[1].getbalance()
        address = self.nodes[1].getnewaddress("normal")
        fee_per_byte = Decimal('0.001') / 1000
        self.nodes[0].settxfee(fee_per_byte * 1000)
        self.nodes[0].sendtoaddress(address, 1, "", "", False)
        self.nodes[0].generate(1)
        self.sync_all()
        node_1_balance += Decimal("1.0")
        assert_equal(self.nodes[1].getbalance(), node_1_balance)

        # Send 1 BCH to a "token aware" p2pkh address owned by node1, and verify node1 sees the balance change
        address = self.nodes[1].getnewaddress("tokan-aware-p2pkh")
        prefix, _, addr_hash = cashaddr.decode(address)
        token_address = cashaddr.encode(prefix, cashaddr.TOKEN_PUBKEY_TYPE, addr_hash)
        assert_not_equal(address, token_address)
        assert not self.nodes[0].validateaddress(address)["istokenaware"]
        assert self.nodes[0].validateaddress(token_address)["istokenaware"]
        self.nodes[0].sendtoaddress(token_address, 1.0, "", "", False)
        self.nodes[0].generate(1)
        self.sync_all()
        node_1_balance += Decimal("1.0")
        assert_equal(self.nodes[1].getbalance(), node_1_balance)

        # Send 1 BCH to a "token aware" p2sh address owned by node1, and verify node1 sees the balance change
        address = self.nodes[1].getnewaddress("token-aware-p2pkh-2")
        p2sh_address = self.nodes[1].addmultisigaddress(1, [address], "token-aware-p2sh")
        prefix, _, addr_hash = cashaddr.decode(p2sh_address)
        token_p2sh_address = cashaddr.encode(prefix, cashaddr.TOKEN_SCRIPT_TYPE, addr_hash)
        assert_not_equal(p2sh_address, token_p2sh_address)
        assert not self.nodes[0].validateaddress(p2sh_address)["istokenaware"]
        assert self.nodes[0].validateaddress(p2sh_address)["isscript"]
        assert self.nodes[0].validateaddress(token_p2sh_address)["istokenaware"]
        assert self.nodes[0].validateaddress(token_p2sh_address)["isscript"]
        self.nodes[0].sendtoaddress(token_p2sh_address, 1.0, "", "", False)
        self.nodes[0].generate(1)
        self.sync_all()
        node_1_balance += Decimal("1.0")
        assert_equal(self.nodes[1].getbalance(), node_1_balance)


if __name__ == '__main__':
    WalletSendToTokenAwareCashAddr().main()
