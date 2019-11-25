#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Copyright (c) 2019 The BitcoinUnlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

if [ $DIST != "RPM" ]; then
  export LC_ALL=C.UTF-8
fi

cd "build" || (echo "could not enter distdir build"; exit 1)

# Create location to place core files, and change the kernel setting to direct
# cores into the newly created directory
mkdir /tmp/cores
DOCKER_EXEC mkdir /tmp/cores
echo '/tmp/cores/core.%e.%p.%h.%t' | sudo tee /proc/sys/kernel/core_pattern

if [ "$RUN_TESTS" = "true" ]; then
    BEGIN_FOLD unit-tests
    echo "Unit tests"
    if [ $HOST != "x86_64-w64-mingw32" ]; then
        DOCKER_EXEC LD_LIBRARY_PATH=$TRAVIS_BUILD_DIR/depends/$HOST/lib make $MAKEJOBS check VERBOSE=1;
    fi
    END_FOLD
fi

if [ "$RUN_TESTS" = "true" ]; then
    BEGIN_FOLD functional-tests
    echo "QA python functional tests"
    DOCKER_EXEC qa/pull-tester/rpc-tests.py --coverage --no-ipv6-rpc-listen;
    END_FOLD
fi

cd ${TRAVIS_BUILD_DIR} || (echo "could not enter travis build dir $TRAVIS_BUILD_DIR"; exit 1)
