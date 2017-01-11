#!/bin/sh

#Convert paths from Windows style to POSIX style
PATH_DEPS=$(echo "/$PATH_DEPS" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
BITCOIN_GIT_ROOT=$(echo "/$BITCOIN_GIT_ROOT" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')

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
	if [ -n "$CHECK" ]; then
		echo 'Enabling tests in ./configure command'
		DISABLE_TESTS=
	fi

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
	--disable-upnp-default \
	$DISABLE_TESTS \
	--with-qt-incdir="$PATH_DEPS/Qt/5.3.2/include" \
	--with-qt-libdir="$PATH_DEPS/Qt/5.3.2/lib" \
	--with-qt-plugindir="$PATH_DEPS/Qt/5.3.2/plugins" \
	--with-qt-bindir="$PATH_DEPS/Qt/5.3.2/bin" \
	--with-protoc-bindir="$PATH_DEPS/protobuf-2.6.1/src"
fi

make $MAKE_CORES

# Optinally run make check tests (REVISIT: currently not working due to issues with python)
# NOTE: This will only function if you have built BOOST with tests enabled
#       and have the correct version of python installed and in the Windows PATH
#if [ -n "$CHECK" ]; then
#	echo 'Running make check tests'
#	make check
#fi

# Strip symbol tables
if [ -n "$STRIP" ]; then
	echo 'Stripping exeutables'
	strip src/bitcoin-tx.exe
	strip src/bitcoin-cli.exe
	strip src/bitcoind.exe
	strip src/qt/bitcoin-qt.exe
fi
