#!/bin/bash
#
# Copyright (c) 2018 The BitcoinUnlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

apt-get update
apt-get install -y --no-install-recommends --no-upgrade -qq build-essential libtool autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils curl git ca-certificates ccache python3
