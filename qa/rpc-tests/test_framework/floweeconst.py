#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php

#
# Const for Floweethehub integration
#
BITCOIN_CONF = "flowee.conf"  #see SettingsDefaults.h
HUB_LOG = "hub.log"           # debug.log is used by other clients
BCD_HUB_PATH = "/hub/debug/src/bitcoind"
NODE_NUM = 3               # node3 is for Hub
CONF_IN_CACHE = "cache/node" + str(NODE_NUM) + "/" + BITCOIN_CONF
# unrecoginized parameters by flowee
FILTER_PARAM_KEYS = ['usecashaddr', 'maxlimitertxfee', 'debug']
