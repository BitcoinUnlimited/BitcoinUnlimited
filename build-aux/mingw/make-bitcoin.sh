#!/bin/sh

#Convert paths from Windows style to POSIX style
MSYS_BIN=$(echo "/$MSYS_BIN" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
PATH_DEPS=$(echo "/$PATH_DEPS" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
TOOLCHAIN_BIN=$(echo "/$TOOLCHAIN_BIN" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
BITCOIN_GIT_ROOT=$(echo "/$BITCOIN_GIT_ROOT" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')

# Set PATH using POSIX style paths
PATH="$TOOLCHAIN_BIN:$MSYS_BIN:$PATH"

# Verify that required dependencies have been built
CHECK_PATH="$PATH_DEPS/openssl-1.0.1k/libssl.a"
if [ ! -e "$CHECK_PATH" ]
then
	echo OpenSSL dependency is missing.  Please run config-mingw.bat.
	exit -1
fi
CHECK_PATH="$PATH_DEPS/libevent-2.0.22/.libs/libevent.a"
if [ ! -e "$CHECK_PATH" ]
then
	echo LibEvent dependency is missing.  Please run config-mingw.bat.
	exit -1
fi
CHECK_PATH="$PATH_DEPS/miniupnpc/libminiupnpc.a"
if [ ! -e "$CHECK_PATH" ]
then
	echo MiniUPNPC dependency is missing.  Please run config-mingw.bat.
	exit -1
fi
CHECK_PATH="$PATH_DEPS/protobuf-2.6.1/src/.libs/libprotobuf.a"
if [ ! -e "$CHECK_PATH" ]
then
	echo Protobuf dependency is missing.  Please run config-mingw.bat.
	exit -1
fi
CHECK_PATH="$PATH_DEPS/libpng-1.6.16/.libs/libpng.a"
if [ ! -e "$CHECK_PATH" ]
then
	echo LibPNG dependency is missing.  Please run config-mingw.bat.
	exit -1
fi
CHECK_PATH="$PATH_DEPS/qrencode-3.4.4/.libs/libqrencode.a"
if [ ! -e "$CHECK_PATH" ]
then
	echo LibQREncode dependency is missing.  Please run config-mingw.bat.
	exit -1
fi
CHECK_PATH="$PATH_DEPS/boost_1_61_0/bin.v2/libs/chrono/build/gcc-mingw-4.9.2/release/link-static/runtime-link-static/threading-multi/libboost_chrono-mgw49-mt-s-1_61.a"
if [ ! -e "$CHECK_PATH" ]
then
	echo Boost dependency is missing.  Please run config-mingw.bat.
	exit -1
fi
CHECK_PATH="$PATH_DEPS/Qt/5.3.2/lib/libQt5Core.a"
if [ ! -e "$CHECK_PATH" ]
then
	echo Qt dependency is missing.  Please run config-mingw.bat.
	exit -1
fi

#If skip configure is set, then skip autogen MUST be set
if [ -n "$SKIP_CONFIGURE" ]; then
	SKIP_AUTOGEN=YES
fi

# Build BitcoinUnlimited
cd "$BITCOIN_GIT_ROOT"

#define and export BOOST_ROOT prior to any calls that require
#executing ./configure (this may include `make clean`) depending on current system state
export BOOST_ROOT="$PATH_DEPS/boost_1_61_0"

#if the clean parameter was passed call clean prior to make
if [ -n "$CLEAN_BUILD" ]; then
	echo 'Cleaning build...'
	make clean
	make distclean
fi

#skip autogen (improve build speed if this step isn't necessary)
if [ -z "$SKIP_AUTOGEN" ]; then
	echo 'Running autogen...'
	./autogen.sh
fi

# NOTE: If you want to run tests (make check and rpc-tests) you must
#       1. Have built boost with the --with-tests flag (in config-mingw.bat)
#       2. Have built a Hexdump equivalent for mingw (included by default in install-deps.sh)
if [ -z "$SKIP_CONFIGURE" ]; then
	echo 'Running configure...'
	# By default build without tests
	DISABLE_TESTS="--disable-tests"
	# However, if the --check argument was specified, we will run "make check"
	# which means we need to configure for build with tests enabled
	if [ -n "$ENABLE_TESTS" ]; then
		echo 'Enabling tests in ./configure command'
		DISABLE_TESTS=
	fi
	
	# Uncomment below to build release
	ENABLE_DEBUG=
	# Uncomment below to build debug
	#ENABLE_DEBUG="--enable-debug"

	CPPFLAGS="-I$PATH_DEPS/db-4.8.30.NC/build_unix \
	-I$PATH_DEPS/openssl-1.0.1k/include \
	-I$PATH_DEPS/libevent-2.0.22/include \
	-I$PATH_DEPS \
	-I$PATH_DEPS/protobuf-2.6.1/src \
	-I$PATH_DEPS/libpng-1.6.16 \
	-I$PATH_DEPS/qrencode-3.4.4" \
	LDFLAGS="-L$PATH_DEPS/db-4.8.30.NC/build_unix \
	-L$PATH_DEPS/openssl-1.0.1k \
	-L$PATH_DEPS/libevent-2.0.22/.libs \
	-L$PATH_DEPS/miniupnpc \
	-L$PATH_DEPS/protobuf-2.6.1/src/.libs \
	-L$PATH_DEPS/libpng-1.6.16/.libs \
	-L$PATH_DEPS/qrencode-3.4.4/.libs" \
	BOOST_ROOT="$PATH_DEPS/boost_1_61_0" \
	./configure \
	$ENABLE_DEBUG \
	--disable-upnp-default \
	$DISABLE_TESTS \
	--with-qt-incdir="$PATH_DEPS/Qt/5.3.2/include" \
	--with-qt-libdir="$PATH_DEPS/Qt/5.3.2/lib" \
	--with-qt-plugindir="$PATH_DEPS/Qt/5.3.2/plugins" \
	--with-qt-bindir="$PATH_DEPS/Qt/5.3.2/bin" \
	--with-protoc-bindir="$PATH_DEPS/protobuf-2.6.1/src"
fi

make $MAKE_CORES

# Optinally run make check tests (REVISIT: currently not working due to issues with python3 scripts)
# NOTE: This will only function if you have built BOOST with tests enabled
#       and have the correct version of python installed and in the Windows PATH
#if [ -n "$ENABLE_TESTS" ]; then
#	# Skip make check for now since it doesn't work on Windows
#	#echo 'Running make check tests'
#	#make check
#	
#	#echo 'Running qa tests'
#	#./qa/pull-tester/rpc-tests.py --win
#fi

# Strip symbol tables
if [ -n "$STRIP" ]; then
	echo 'Stripping exeutables'
	strip src/bitcoin-tx.exe
	strip src/bitcoin-cli.exe
	strip src/bitcoind.exe
	strip src/qt/bitcoin-qt.exe
fi
