#!/bin/sh
for i in "$@"
do
case $i in
    --path-deps=*)
    PATH_DEPS="${i#*=}"
    shift # past argument=value
    ;;
    --path-bitcoin=*)
    PATH_BITCOIN="${i#*=}"
    shift # past argument=value
    ;;
    --path-msys=*)
    MSYS_BIN="${i#*=}"
    shift # past argument=value
    ;;
    --path-mingw=*)
    MINGW_BIN="${i#*=}"
    shift # past argument=value
    ;;
    --path-toolchain=*)
    TOOLCHAIN_BIN="${i#*=}"
    shift # past argument=value
    ;;
    --strip)
    STRIP=YES
    shift # past argument=value
    ;;
    --check)
    CHECK=YES
    shift # past argument=value
    ;;
    --no-autogen)
    SKIP_AUTOGEN=YES
    shift # past argument=value
    ;;
    --no-configure)
	SKIP_AUTOGEN=YES # if configure is off, then we cannot run autogen
    SKIP_CONFIGURE=YES
    shift # past argument=value
    ;;
    --default)
    DEFAULT=YES
    shift # past argument with no value
    ;;
    *)
            # unknown option
    ;;
esac
done

PATH=$TOOLCHAIN_BIN:$MSYS_BIN:$MINGW_BIN:$PATH

# Build BitcoinUnlimited
cd $PATH_BITCOIN
if [ -z "$SKIP_AUTOGEN" ]; then
	./autogen.sh
fi

BOOST_ROOT=$PATH_DEPS/boost_1_61_0

# NOTE: If you want to run tests (make check and rpc-tests) you must
#       1. Have built boost with the --with-tests flag (in config-mingw.bat)
#       2. Have built a Hexdump equivalent for mingw (included by default in install-deps.sh)
if [ -z "$SKIP_CONFIGURE" ]; then
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
	BOOST_ROOT=$PATH_DEPS/boost_1_61_0 \
	./configure \
	--disable-upnp-default \
	$DISABLE_TESTS \
	--with-qt-incdir=$PATH_DEPS/Qt/5.3.2/include \
	--with-qt-libdir=$PATH_DEPS/Qt/5.3.2/lib \
	--with-qt-plugindir=$PATH_DEPS/Qt/5.3.2/plugins \
	--with-qt-bindir=$PATH_DEPS/Qt/5.3.2/bin \
	--with-protoc-bindir=$PATH_DEPS/protobuf-2.6.1/src
fi

make -j4

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
