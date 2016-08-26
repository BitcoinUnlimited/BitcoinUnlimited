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
./autogen.sh
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
--disable-tests \
--with-qt-incdir=$PATH_DEPS/Qt/5.3.2/include \
--with-qt-libdir=$PATH_DEPS/Qt/5.3.2/lib \
--with-qt-plugindir=$PATH_DEPS/Qt/5.3.2/plugins \
--with-qt-bindir=$PATH_DEPS/Qt/5.3.2/bin \
--with-protoc-bindir=$PATH_DEPS/protobuf-2.6.1/src

make -j4

# Strip symbol tables
strip src/bitcoin-cli.exe
strip src/bitcoind.exe
strip src/qt/bitcoin-qt.exe
