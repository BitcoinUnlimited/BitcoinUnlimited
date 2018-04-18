#!/bin/sh

#Convert paths from Windows style to POSIX style
MSYS_BIN=$(echo "/$MSYS_BIN" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
PATH_DEPS=$(echo "/$PATH_DEPS" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
TOOLCHAIN_BIN=$(echo "/$TOOLCHAIN_BIN" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
BITCOIN_GIT_ROOT=$(echo "/$BITCOIN_GIT_ROOT" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')

# Set PATH using POSIX style paths
PATH="$TOOLCHAIN_BIN:$MSYS_BIN:$PATH"

# Build BitcoinUnlimited
cd "$BITCOIN_GIT_ROOT"

#define and export BOOST_ROOT prior to any calls that require
#executing ./configure (this may include `make clean`) depending on current system state
export BOOST_ROOT="$PATH_DEPS/boost_1_61_0"

#if the clean parameter was passed call clean prior to make
echo 'Cleaning build...'
make clean
make distclean
