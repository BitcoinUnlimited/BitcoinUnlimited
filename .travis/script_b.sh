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

BEGIN_FOLD unit-tests
if [ "$RUN_TESTS" = "true" ] && ! { [ "$HOST" = "i686-w64-mingw32" ] || [ "$HOST" = "x86_64-w64-mingw32" ]; }; then
  travis_wait 50 DOCKER_EXEC LD_LIBRARY_PATH=$TRAVIS_BUILD_DIR/depends/$HOST/lib make $MAKEJOBS check VERBOSE=1;
fi
END_FOLD

BEGIN_FOLD functional-tests
if [ "$RUN_TESTS" = "true" ]; then DOCKER_EXEC qa/pull-tester/rpc-tests.py --coverage --no-ipv6-rpc-listen; fi
END_FOLD

