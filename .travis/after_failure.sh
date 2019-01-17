#!/usr/bin/env bash
#
# Copyright (c) 2019 The BitcoinUnlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

for i in `find /home/travis/ -name debug.log`; do echo $i; echo "-----"; tail -100 $i; done
for i in `find /tmp/ -name debug.log`; do echo $i; echo "-----"; tail -100 $i; done
for i in `find /home/travis/ -name bitcoin.conf`; do echo $i; echo "-----"; cat $i; done
for i in `find /tmp/ -name bitcoin.conf`; do echo $i; echo "-----"; cat $i; done
