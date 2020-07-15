#!/bin/bash
#
# Copyright (c) 2018 The BitcoinUnlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

apt-get update
apt-get install -y libedit2 libminiupnpc-dev libqrencode-dev libprotobuf-dev protobuf-compiler libboost-all-dev libdb5.3++-dev libdb5.3-dev python3-zmq libzmq3-dev libssl1.0-dev libevent-dev bsdmainutils
apt-get install software-properties-common -y
add-apt-repository ppa:bitcoin-unlimited/bu-ppa
apt-get update -y
apt-get install libdb4.8-dev libdb4.8++-dev -y

