#!/usr/bin/env bash
#
# Copyright (c) 2019 The BitcoinUnlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

if [ $DIST != "RPM" ]; then
  export LC_ALL=C.UTF-8
fi

echo $TRAVIS_COMMIT_RANGE
echo $TRAVIS_COMMIT_LOG
