#!/bin/sh
for i in "$@"
do
case $i in
    --path-7zip=*)
    CMD_7ZIP="${i#*=}"
    shift # past argument=value
    ;;
    --path-deps=*)
    PATH_DEPS="${i#*=}"
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

# Ensure dependency directory exists
mkdir -p $PATH_DEPS
cd $PATH_DEPS

# Open SSL (Download, unpack, and build)
cd $PATH_DEPS
wget --no-check-certificate https://www.openssl.org/source/openssl-1.0.1k.tar.gz
tar xvfz openssl-1.0.1k.tar.gz
cd $PATH_DEPS/openssl-1.0.1k
./Configure no-zlib no-shared no-dso no-krb5 no-camellia no-capieng no-cast no-cms no-dtls1 no-gost no-gmp no-heartbeats no-idea no-jpake no-md2 no-mdc2 no-rc5 no-rdrand no-rfc3779 no-rsax no-sctp no-seed no-sha0 no-static_engine no-whirlpool no-rc2 no-rc4 no-ssl2 no-ssl3 mingw
make


# Berkeley DB (Download, unpack, and build)
cd $PATH_DEPS
wget http://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz
tar xvfz db-4.8.30.NC.tar.gz
cd $PATH_DEPS/db-4.8.30.NC/build_unix
../dist/configure --enable-mingw --enable-cxx --disable-shared --disable-replication
make


# Boost (Download and unpack - build requires Windows CMD)
cd $PATH_DEPS
wget --no-check-certificate https://sourceforge.net/projects/boost/files/boost/1.61.0/boost_1_61_0.zip/download
"$CMD_7ZIP" x boost_1_61_0.zip -aoa


# Libevent (Download, unpack, and build)
cd $PATH_DEPS
wget --no-check-certificate https://sourceforge.net/projects/levent/files/release-2.0.22-stable/libevent-2.0.22-stable.tar.gz/download
tar -xvf libevent-2.0.22-stable.tar.gz 
mv libevent-2.0.22-stable libevent-2.0.22
cd $PATH_DEPS/libevent-2.0.22
./configure --disable-shared
make


# Miniunpuc (Download, unpack, and rename - build requires Windows CMD)
cd $PATH_DEPS
wget --no-check-certificate http://miniupnp.free.fr/files/download.php?file=miniupnpc-1.9.20151008.tar.gz
tar -xvf miniupnpc-1.9.20151008.tar.gz
mv miniupnpc-1.9.20151008 miniupnpc


# Protobuf (Download, unpack, and build)
cd $PATH_DEPS
wget --no-check-certificate -O protobuf-2.6.1.tar.gz https://github.com/google/protobuf/releases/download/v2.6.1/protobuf-2.6.1.tar.gz
tar xvfz protobuf-2.6.1.tar.gz
cd $PATH_DEPS/protobuf-2.6.1
./configure --disable-shared
make


# Libpng (Download, unpack, build, and rename-copy)
cd $PATH_DEPS
wget --no-check-certificate http://download.sourceforge.net/libpng/libpng-1.6.16.tar.gz
tar -xvf libpng-1.6.16.tar.gz
cd $PATH_DEPS/libpng-1.6.16
./configure --disable-shared
make
cp .libs/libpng16.a .libs/libpng.a


# Qrencode (Download, unpack, and build)
cd $PATH_DEPS
wget --no-check-certificate http://fukuchi.org/works/qrencode/qrencode-3.4.4.tar.gz
tar -xvf qrencode-3.4.4.tar.gz
cd $PATH_DEPS/qrencode-3.4.4
LIBS="../libpng-1.6.16/.libs/libpng.a ../../mingw32/i686-w64-mingw32/lib/libz.a" \
png_CFLAGS="-I../libpng-1.6.16" \
png_LIBS="-L../libpng-1.6.16/.libs" \
./configure --enable-static --disable-shared --without-tools
make


# Qt (Download, unpack, and rename - build requires Windows CMD)
cd $PATH_DEPS
wget --no-check-certificate http://download.qt-project.org/archive/qt/5.3/5.3.2/submodules/qtbase-opensource-src-5.3.2.7z
wget --no-check-certificate http://download.qt-project.org/archive/qt/5.3/5.3.2/submodules/qttools-opensource-src-5.3.2.7z
"$CMD_7ZIP" x qtbase-opensource-src-5.3.2.7z -aoa -o$PATH_DEPS/Qt
"$CMD_7ZIP" x qttools-opensource-src-5.3.2.7z -aoa -o$PATH_DEPS/Qt
cd Qt
mv qtbase-opensource-src-5.3.2 5.3.2
