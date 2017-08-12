#!/bin/sh

#Convert paths from Windows style to POSIX style
MSYS_BIN=$(echo "/$MSYS_BIN" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
DEPS_ROOT=$(echo "/$DEPS_ROOT" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
PATH_DEPS=$(echo "/$PATH_DEPS" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
CMD_7ZIP=$(echo "/$CMD_7ZIP" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
TOOLCHAIN_BIN=$(echo "/$TOOLCHAIN_BIN" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')

# Set PATH using POSIX style paths
PATH="$TOOLCHAIN_BIN:$MSYS_BIN:$PATH"

# Strip the /bin sub directory
TOOLCHAIN_ROOT=${TOOLCHAIN_BIN%/*}
# Detect if this is 32 or 64 target build
if [ "$(basename $TOOLCHAIN_ROOT)" = "mingw32" ]
then
	LIBZ_STATIC_LIB="$TOOLCHAIN_ROOT/i686-w64-mingw32/lib/libz.a"
	# OpenSSL 1.0.1 requires correctly specifying the version of MinGW
	LIB_SSL_MINGW=mingw
else
	LIBZ_STATIC_LIB="$TOOLCHAIN_ROOT/x86_64-w64-mingw32/lib/libz.a"
	# OpenSSL 1.0.1 requires correctly specifying the version of MinGW
	LIB_SSL_MINGW=mingw64
fi

# Ensure dependency directory exists
mkdir -p "$PATH_DEPS"
cd "$PATH_DEPS"

# Hexdump (Download, unpack, and build into the toolchain path)
# NOTE: Hexdump is only needed if you intend to build with unit tests enabled.
cd "$DEPS_ROOT"
# don't download if already downloaded
if [ ! -e hexdump.zip ]
then
	wget --no-check-certificate https://github.com/wahern/hexdump/archive/master.zip -O "$DEPS_ROOT/hexdump.zip"
fi
# don't extract if already extracted
cd "$PATH_DEPS"
if [ ! -d hexdump-master ]
then
	cd "$DEPS_ROOT"
	"$CMD_7ZIP" x hexdump.zip -aoa -o"$PATH_DEPS"
fi
cd "$PATH_DEPS/hexdump-master"
gcc -std=gnu99 -g -O2 -Wall -Wextra -Werror -Wno-unused-variable -Wno-unused-parameter hexdump.c -DHEXDUMP_MAIN -o "$MSYS_BIN/hexdump.exe"


# Open SSL (Download, unpack, and build)
cd "$DEPS_ROOT"
# don't download if already downloaded
if [ ! -e openssl-1.0.1k.tar.gz ]
then
	wget --no-check-certificate https://www.openssl.org/source/openssl-1.0.1k.tar.gz -O "$DEPS_ROOT/openssl-1.0.1k.tar.gz"
fi
# don't extract if already extracted
cd "$PATH_DEPS"
if [ ! -d openssl-1.0.1k ]
then
	cd "$DEPS_ROOT"
	tar xvfz openssl-1.0.1k.tar.gz -C "$PATH_DEPS"
fi
cd "$PATH_DEPS/openssl-1.0.1k"
./Configure no-zlib no-shared no-dso no-krb5 no-camellia no-capieng no-cast no-cms no-dtls1 no-gost no-gmp no-heartbeats no-idea no-jpake no-md2 no-mdc2 no-rc5 no-rdrand no-rfc3779 no-rsax no-sctp no-seed no-sha0 no-static_engine no-whirlpool no-rc2 no-rc4 no-ssl2 no-ssl3 $LIB_SSL_MINGW
make $MAKE_CORES
#pause for debugging purposes
#read -rsp $'Press any key to continue...\n' -n 1 key


# Berkeley DB (Download, unpack, and build)
cd "$DEPS_ROOT"
# don't download if already downloaded
if [ ! -e db-4.8.30.NC.tar.gz ]
then
	wget http://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz -O "$DEPS_ROOT/db-4.8.30.NC.tar.gz"
fi
# don't extract if already extracted
cd "$PATH_DEPS"
if [ ! -d db-4.8.30.NC ]
then
	cd "$DEPS_ROOT"
	tar xvfz db-4.8.30.NC.tar.gz -C "$PATH_DEPS"
fi
cd "$PATH_DEPS/db-4.8.30.NC/build_unix"
../dist/configure --enable-mingw --enable-cxx --disable-shared --disable-replication
make $MAKE_CORES
#pause for debugging purposes
#read -rsp $'Press any key to continue...\n' -n 1 key


# Boost (Download and unpack - build requires Windows CMD)
cd "$DEPS_ROOT"
# don't download if already downloaded
if [ ! -e boost_1_61_0.zip ]
then
	wget --no-check-certificate https://sourceforge.net/projects/boost/files/boost/1.61.0/boost_1_61_0.zip/download -O "$DEPS_ROOT/boost_1_61_0.zip"
fi
# don't extract if already extracted
cd "$PATH_DEPS"
if [ ! -d boost_1_61_0 ]
then
	cd "$DEPS_ROOT"
	"$CMD_7ZIP" x boost_1_61_0.zip -aoa -o"$PATH_DEPS"
fi


# Libevent (Download, unpack, and build)
cd "$DEPS_ROOT"
# don't download if already downloaded
if [ ! -e libevent-2.0.22-stable.tar.gz ]
then
	wget --no-check-certificate https://sourceforge.net/projects/levent/files/release-2.0.22-stable/libevent-2.0.22-stable.tar.gz/download -O "$DEPS_ROOT/libevent-2.0.22-stable.tar.gz"
fi
# don't extract if already extracted
cd "$PATH_DEPS"
if [ ! -d libevent-2.0.22 ]
then
	cd "$DEPS_ROOT"
	tar -xvf libevent-2.0.22-stable.tar.gz -C "$PATH_DEPS"
	cd "$PATH_DEPS"
	mv libevent-2.0.22-stable libevent-2.0.22
fi
cd "$PATH_DEPS/libevent-2.0.22"
./configure --disable-shared
make $MAKE_CORES
#pause for debugging purposes
#read -rsp $'Press any key to continue...\n' -n 1 key


# Miniunpuc (Download, unpack, and rename - build requires Windows CMD)
cd "$DEPS_ROOT"
# don't download if already downloaded
# Updated version 2.0.20170509 for CVE-2017-8798
if [ ! -e miniupnpc-2.0.20170509.tar.gz ]
then
	wget --no-check-certificate http://miniupnp.free.fr/files/download.php?file=miniupnpc-2.0.20170509.tar.gz -O "$DEPS_ROOT/miniupnpc-2.0.20170509.tar.gz"
fi
# don't extract if already extracted
cd "$PATH_DEPS"
if [ ! -d miniupnpc ]
then
	cd "$DEPS_ROOT"
	tar -xvf miniupnpc-2.0.20170509.tar.gz -C "$PATH_DEPS"
	cd "$PATH_DEPS"
	mv miniupnpc-2.0.20170509 miniupnpc
fi
#pause for debugging purposes
#read -rsp $'Press any key to continue...\n' -n 1 key


# Protobuf (Download, unpack, and build)
cd "$DEPS_ROOT"
# don't download if already downloaded
if [ ! -e protobuf-2.6.1.tar.gz ]
then
	wget --no-check-certificate -O protobuf-2.6.1.tar.gz https://github.com/google/protobuf/releases/download/v2.6.1/protobuf-2.6.1.tar.gz -O "$DEPS_ROOT/protobuf-2.6.1.tar.gz"
fi
# don't extract if already extracted
cd "$PATH_DEPS"
if [ ! -d protobuf-2.6.1 ]
then
	cd "$DEPS_ROOT"
	tar xvfz protobuf-2.6.1.tar.gz -C "$PATH_DEPS"
fi
cd "$PATH_DEPS/protobuf-2.6.1"
./configure --disable-shared
make $MAKE_CORES
#pause for debugging purposes
#read -rsp $'Press any key to continue...\n' -n 1 key


# Libpng (Download, unpack, build, and rename-copy)
cd "$DEPS_ROOT"
# don't download if already downloaded
if [ ! -e libpng-1.6.16.tar.gz ]
then
	wget --no-check-certificate http://download.sourceforge.net/libpng/libpng-1.6.16.tar.gz -O "$DEPS_ROOT/libpng-1.6.16.tar.gz"
fi
# don't extract if already extracted
cd "$PATH_DEPS"
if [ ! -d libpng-1.6.16 ]
then
	cd "$DEPS_ROOT"
	tar -xvf libpng-1.6.16.tar.gz -C "$PATH_DEPS"
fi
cd "$PATH_DEPS/libpng-1.6.16"
./configure --disable-shared
make $MAKE_CORES
cp .libs/libpng16.a .libs/libpng.a
#pause for debugging purposes
#read -rsp $'Press any key to continue...\n' -n 1 key


# Qrencode (Download, unpack, and build)
cd "$DEPS_ROOT"
# don't download if already downloaded
if [ ! -e qrencode-3.4.4.tar.gz ]
then
	wget --no-check-certificate http://fukuchi.org/works/qrencode/qrencode-3.4.4.tar.gz -O "$DEPS_ROOT/qrencode-3.4.4.tar.gz"
fi
# don't extract if already extracted
cd "$PATH_DEPS"
if [ ! -d qrencode-3.4.4 ]
then
	cd "$DEPS_ROOT"
	tar -xvf qrencode-3.4.4.tar.gz -C "$PATH_DEPS"
fi
cd "$PATH_DEPS/qrencode-3.4.4"
LIBS="../libpng-1.6.16/.libs/libpng.a $LIBZ_STATIC_LIB" \
png_CFLAGS="-I../libpng-1.6.16" \
png_LIBS="-L../libpng-1.6.16/.libs" \
./configure --enable-static --disable-shared --without-tools
make $MAKE_CORES
#pause for debugging purposes
#read -rsp $'Press any key to continue...\n' -n 1 key


# Qt (Download, unpack, and rename - build requires Windows CMD)
cd "$DEPS_ROOT"
# don't download if already downloaded
if [ ! -e qttools-opensource-src-5.3.2.7z ]
then
	wget --no-check-certificate http://download.qt-project.org/archive/qt/5.3/5.3.2/submodules/qttools-opensource-src-5.3.2.7z -O "$DEPS_ROOT/qttools-opensource-src-5.3.2.7z"
fi
# don't extract if already extracted
cd "$PATH_DEPS/Qt"
if [ ! -d "qttools-opensource-src-5.3.2" ]
then
	cd "$DEPS_ROOT"
	"$CMD_7ZIP" x qttools-opensource-src-5.3.2.7z -aoa -o"$PATH_DEPS/Qt"
fi

cd "$DEPS_ROOT"
# don't download if already downloaded
if [ ! -e qtbase-opensource-src-5.3.2.7z ]
then
	wget --no-check-certificate http://download.qt-project.org/archive/qt/5.3/5.3.2/submodules/qtbase-opensource-src-5.3.2.7z -O "$DEPS_ROOT/qtbase-opensource-src-5.3.2.7z"
fi
# don't extract if already extracted
cd "$PATH_DEPS/Qt"
if [ ! -d "5.3.2" ]
then
	cd "$DEPS_ROOT"
	"$CMD_7ZIP" x qtbase-opensource-src-5.3.2.7z -aoa -o"$PATH_DEPS/Qt"
	cd "$PATH_DEPS/Qt"
	mv qtbase-opensource-src-5.3.2 5.3.2
fi
