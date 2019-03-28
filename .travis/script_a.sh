#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Copyright (c) 2019 The BitcoinUnlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

export LC_ALL=C.UTF-8

BEGIN_FOLD autogen
export TRAVIS_COMMIT_LOG=`git log --format=fuller -1`
if [ -n "$USE_SHELL" ]; then export CONFIG_SHELL="$USE_SHELL"; fi
OUTDIR=$BASE_OUTDIR/$TRAVIS_PULL_REQUEST/$TRAVIS_JOB_NUMBER-$HOST
BITCOIN_CONFIG_ALL="--disable-dependency-tracking --prefix=$TRAVIS_BUILD_DIR/depends/$HOST --bindir=$OUTDIR/bin --libdir=$OUTDIR/lib";
if [ "$USE_CLANG" = "false" ]; then DOCKER_EXEC ccache --max-size=$CCACHE_SIZE; fi
test -n "$USE_SHELL" && DOCKER_EXEC "$CONFIG_SHELL" -c "./autogen.sh 2>&1 > autogen.out" || ./autogen.sh 2>&1 > autogen.out || (cat autogen.out && false)
END_FOLD

mkdir build && cd build
echo "BITCOIN_CONFIG_ALL=$BITCOIN_CONFIG_ALL"
echo "BITCOIN_CONFIG=$BITCOIN_CONFIG"
echo "GOAL=$GOAL"

BEGIN_FOLD configure
DOCKER_EXEC ../configure --cache-file=config.cache $BITCOIN_CONFIG_ALL $BITCOIN_CONFIG || ( cat config.log && false)
if [ "$HOST" = "x86_64-apple-darwin11" ]; then
  docker exec $DOCKER_ID bash -c "$TRAVIS_BUILD_DIR/contrib/devtools/xversionkeys.py > $TRAVIS_BUILD_DIR/src/xversionkeys.h < $TRAVIS_BUILD_DIR/src/xversionkeys.dat" ;
fi
END_FOLD

BEGIN_FOLD formatting-check
if [ "$RUN_FORMATTING_CHECK" = "true" ]; then DOCKER_EXEC make $MAKEJOBS check-formatting VERBOSE=1; fi
END_FOLD

BEGIN_FOLD make
DOCKER_EXEC make $MAKEJOBS $GOAL || ( echo "Build failure. Verbose build follows." && DOCKER_EXEC make $GOAL V=1 ; false ) ;
if [ "$RUN_TESTS" = "true" ] && { [ "$HOST" = "i686-w64-mingw32" ] || [ "$HOST" = "x86_64-w64-mingw32" ]; }; then
  travis_wait 50 DOCKER_EXEC LD_LIBRARY_PATH=$TRAVIS_BUILD_DIR/depends/$HOST/lib make $MAKEJOBS check VERBOSE=1;
fi
END_FOLD

cd ${TRAVIS_BUILD_DIR} || (echo "could not enter travis build dir $TRAVIS_BUILD_DIR"; exit 1)
