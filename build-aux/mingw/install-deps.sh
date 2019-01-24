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
	# OpenSSL 1.0.2 requires correctly specifying the version of MinGW
	LIB_SSL_MINGW=mingw
else
	LIBZ_STATIC_LIB="$TOOLCHAIN_ROOT/x86_64-w64-mingw32/lib/libz.a"
	# OpenSSL 1.0.2 requires correctly specifying the version of MinGW
	LIB_SSL_MINGW=mingw64
fi

check_hash() {
	HASH_FILE="$2.hash"
	echo "$1  $2" > "$HASH_FILE"
	shasum -a 256 -c "$HASH_FILE" >/dev/null 2>/dev/null || \
		(echo "Checksum mismatch, aborting install..."; rm -f "$2" "$HASH_FILE") || true
	if [ ! -e "$2" ]
	then
		exit 1
	fi
}

# Ensure dependency directory exists
mkdir -p "$PATH_DEPS"
cd "$PATH_DEPS"

# Hexdump (Download, unpack, and build into the toolchain path)
# NOTE: Hexdump is only needed if you intend to build with unit tests enabled.
cd "$DEPS_ROOT"
# don't download if already downloaded
if [ ! -e hexdump.zip ]
then
	wget --no-check-certificate https://github.com/wahern/hexdump/archive/67157e987d04fba9e4df733a97b410fffb8dfbdf.zip -O "$DEPS_ROOT/hexdump.zip"
	# Verify downloaded file's hash
	# NOTE: This hash was self computed as it was not provided by the author
	# v2016.04.08 sha256=ad2bf521260826e57b8268c8f12810935fcb5a0616137643b6b260ee43034632
	# v2018.12.21 sha256=B582A18D70C51EFE49B482D08FDFAFD35DC4CB0C31DF1DA4F8777DC649893D78
	check_hash B582A18D70C51EFE49B482D08FDFAFD35DC4CB0C31DF1DA4F8777DC649893D78 "$DEPS_ROOT/hexdump.zip"
fi
# don't extract if already extracted
cd "$PATH_DEPS"
if [ ! -d hexdump-67157e987d04fba9e4df733a97b410fffb8dfbdf ]
then
	cd "$DEPS_ROOT"
	"$CMD_7ZIP" x hexdump.zip -aoa -o"$PATH_DEPS"
fi
cd "$PATH_DEPS/hexdump-67157e987d04fba9e4df733a97b410fffb8dfbdf"
gcc -std=gnu99 -g -O2 -Wall -Wextra -Werror -Wno-unused-variable -Wno-unused-parameter hexdump.c -DHEXDUMP_MAIN -o "$MSYS_BIN/hexdump.exe"


# Open SSL (Download, unpack, and build)
cd "$DEPS_ROOT"
# don't download if already downloaded
if [ ! -e openssl-1.0.2o.tar.gz ]
then
	wget --no-check-certificate https://www.openssl.org/source/openssl-1.0.2o.tar.gz -O "$DEPS_ROOT/openssl-1.0.2o.tar.gz"
	# Verify downloaded file's hash (v1.0.1k from depends packages, v1.0.2o from OpenSSL website)
	# v1.0.1k sha256=8f9faeaebad088e772f4ef5e38252d472be4d878c6b3a2718c10a4fcebe7a41c
	# v1.0.2o sha256=ec3f5c9714ba0fd45cb4e087301eb1336c317e0d20b575a125050470e8089e4d
	check_hash ec3f5c9714ba0fd45cb4e087301eb1336c317e0d20b575a125050470e8089e4d "$DEPS_ROOT/openssl-1.0.2o.tar.gz"
fi
# don't extract if already extracted
cd "$PATH_DEPS"
if [ ! -d openssl-1.0.2o ]
then
	cd "$DEPS_ROOT"
	tar xvfz openssl-1.0.2o.tar.gz -C "$PATH_DEPS"
fi
cd "$PATH_DEPS/openssl-1.0.2o"
./Configure no-zlib no-shared no-dso no-krb5 no-camellia no-capieng no-cast no-cms no-dtls1 no-gost no-gmp no-heartbeats no-idea no-jpake no-md2 no-mdc2 no-rc5 no-rdrand no-rfc3779 no-rsax no-sctp no-seed no-sha0 no-static_engine no-whirlpool no-rc2 no-rc4 no-ssl2 no-ssl3 $LIB_SSL_MINGW
make $MAKE_CORES depend
make $MAKE_CORES
#pause for debugging purposes
#read -rsp $'Press any key to continue...\n' -n 1 key


# Berkeley DB (Download, unpack, and build)
cd "$DEPS_ROOT"
# don't download if already downloaded
if [ ! -e db-4.8.30.NC.tar.gz ]
then
	wget http://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz -O "$DEPS_ROOT/db-4.8.30.NC.tar.gz"
	# Verify downloaded file's hash (from depends packages)
	# v4.8.30 sha256=12edc0df75bf9abd7f82f821795bcee50f42cb2e5f76a6a281b85732798364ef
	check_hash 12edc0df75bf9abd7f82f821795bcee50f42cb2e5f76a6a281b85732798364ef "$DEPS_ROOT/db-4.8.30.NC.tar.gz"
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
if [ ! -e boost_1_68_0.zip ]
then
	wget --no-check-certificate https://sourceforge.net/projects/boost/files/boost/1.68.0/boost_1_68_0.zip/download -O "$DEPS_ROOT/boost_1_68_0.zip"
	# Verify downloaded file's hash
	# v1.61.0 sha1=f56f449a203e5009cf6ea16691a022d4928e37f7
	# v1.68.0 sha1=c4cd90922d22ca17d9b8330441637397395e5812
	# NOTE: The sha256 has was self computed, but the sha1 provided by the publisher was verified first
	# v1.61.0 sha256=02d420e6908016d4ac74dfc712eec7d9616a7fc0da78b0a1b5b937536b2e01e8
	# v1.68.0 sha256=3B1DB0B67079266C40B98329D85916E910BBADFC3DB3E860C049056788D4D5CD
	check_hash 3B1DB0B67079266C40B98329D85916E910BBADFC3DB3E860C049056788D4D5CD "$DEPS_ROOT/boost_1_68_0.zip"
fi
# don't extract if already extracted
cd "$PATH_DEPS"
if [ ! -d boost_1_68_0 ]
then
	cd "$DEPS_ROOT"
	"$CMD_7ZIP" x boost_1_68_0.zip -aoa -o"$PATH_DEPS"
fi


# Libevent (Download, unpack, and build)
cd "$DEPS_ROOT"
# don't download if already downloaded
if [ ! -e libevent-2.0.22-stable.tar.gz ]
then
	wget --no-check-certificate https://sourceforge.net/projects/levent/files/release-2.0.22-stable/libevent-2.0.22-stable.tar.gz/download -O "$DEPS_ROOT/libevent-2.0.22-stable.tar.gz"
	# Verify downloaded file's hash (from depends packages)
	# v2.0.22 sha256=71c2c49f0adadacfdbe6332a372c38cf9c8b7895bb73dabeaa53cdcc1d4e1fa3
	check_hash 71c2c49f0adadacfdbe6332a372c38cf9c8b7895bb73dabeaa53cdcc1d4e1fa3 "$DEPS_ROOT/libevent-2.0.22-stable.tar.gz"
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


# Miniupnpc (Download, unpack, and rename - build requires Windows CMD)
cd "$DEPS_ROOT"
# don't download if already downloaded
# Updated version 2.0.20170509 for CVE-2017-8798
if [ ! -e miniupnpc-2.0.20170509.tar.gz ]
then
	wget --no-check-certificate http://miniupnp.free.fr/files/download.php?file=miniupnpc-2.0.20170509.tar.gz -O "$DEPS_ROOT/miniupnpc-2.0.20170509.tar.gz"
	# Verify downloaded file's hash (from depends packages)
	# v2.0.20170509 sha256=d3c368627f5cdfb66d3ebd64ca39ba54d6ff14a61966dbecb8dd296b7039f16a
	check_hash d3c368627f5cdfb66d3ebd64ca39ba54d6ff14a61966dbecb8dd296b7039f16a "$DEPS_ROOT/miniupnpc-2.0.20170509.tar.gz"
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
	# Verify downloaded file's hash
	# NOTE: This hash was self computed as it was not provided by the author
	# v2.6.1 sha256=dbbd7bdd2381633995404de65a945ff1a7610b0da14593051b4738c90c6dd164
	check_hash dbbd7bdd2381633995404de65a945ff1a7610b0da14593051b4738c90c6dd164 "$DEPS_ROOT/protobuf-2.6.1.tar.gz"
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
if [ ! -e libpng-1.6.36.tar.gz ]
then
	wget --no-check-certificate http://download.sourceforge.net/libpng/libpng-1.6.36.tar.gz -O "$DEPS_ROOT/libpng-1.6.36.tar.gz"
	# Verify downloaded file's hash
	# v1.6.16 sha1=50f3b31d013a31e2cac70db177094f6a7618b8be
	# v1.6.36 sha1=65184fe3b20d5dd1a132f271b1b76a0a2dd79447
	# NOTE: The sha256 has was self computed, but the sha1 provided by the publisher was verified first
	# v1.6.16 sha256=02f96b6bad5a381d36d7ba7a5d9be3b06f7fe6c274da00707509c23592a073ad
	# v1.6.36 sha256=CA13C548BDE5FB6FF7117CC0BDAB38808ACB699C0ECCB613F0E4697826E1FD7D
	check_hash CA13C548BDE5FB6FF7117CC0BDAB38808ACB699C0ECCB613F0E4697826E1FD7D "$DEPS_ROOT/libpng-1.6.36.tar.gz"
fi
# don't extract if already extracted
cd "$PATH_DEPS"
if [ ! -d libpng-1.6.36 ]
then
	cd "$DEPS_ROOT"
	tar -xvf libpng-1.6.36.tar.gz -C "$PATH_DEPS"
fi
cd "$PATH_DEPS/libpng-1.6.36"
# Apply patch to fix handling of carriage returns in scripts/dfn.awk
# otherwise MinGW builds on Windows systems will fail
patch -p1 --forward scripts/dfn.awk "$BITCOIN_GIT_ROOT/depends/patches/libpng/mingw-line-ending-fix.patch"

./configure --disable-shared
make $MAKE_CORES
cp .libs/libpng16.a .libs/libpng.a
#pause for debugging purposes
#read -rsp $'Press any key to continue...\n' -n 1 key


# Qrencode (Download, unpack, and build)
cd "$DEPS_ROOT"
# don't download if already downloaded
if [ ! -e qrencode-4.0.2.tar.gz ]
then
	wget --no-check-certificate http://fukuchi.org/works/qrencode/qrencode-4.0.2.tar.gz -O "$DEPS_ROOT/qrencode-4.0.2.tar.gz"
	# Verify downloaded file's hash
	# v3.4.4 sha512=2c7a5bb6a51993a4d44a8e4ef30a3d3e43c55dc726fb7d702cde306a5bfcea1faa5a1bd851aa57c7550c81dadb4cc1cf6ea8afa7b5fa4e9b9c5ed7d9bb6b68cc
	# v4.0.2 sha512=c3e3834574ec059a4b571427b29d6f5f26bd806fd7498b9bba778f4eceab6ebe5733eef0f3c4f6af91eb3f2e9310f93f6d7b337c28e85c72db7e59bd79be77a9
	# NOTE: The sha256 has was self computed, but the sha512 provided by the publisher was verified first
	# v3.4.4 sha256=e794e26a96019013c0e3665cb06b18992668f352c5553d0a553f5d144f7f2a72
	# v4.0.2 sha256=DBABE79C07614625D1F74D8C0AE2EE5358C4E27EAB8FD8FE31F9365F821A3B1D
	check_hash DBABE79C07614625D1F74D8C0AE2EE5358C4E27EAB8FD8FE31F9365F821A3B1D "$DEPS_ROOT/qrencode-4.0.2.tar.gz"
fi
# don't extract if already extracted
cd "$PATH_DEPS"
if [ ! -d qrencode-4.0.2 ]
then
	cd "$DEPS_ROOT"
	tar -xvf qrencode-4.0.2.tar.gz -C "$PATH_DEPS"
fi
cd "$PATH_DEPS/qrencode-4.0.2"
LIBS="../libpng-1.6.36/.libs/libpng.a $LIBZ_STATIC_LIB" \
png_CFLAGS="-I../libpng-1.6.36" \
png_LIBS="-L../libpng-1.6.36/.libs" \
./configure --enable-static --disable-shared --without-tools
make $MAKE_CORES
#pause for debugging purposes
#read -rsp $'Press any key to continue...\n' -n 1 key


# Qt (Download, unpack, and rename - build requires Windows CMD)
cd "$DEPS_ROOT"
# don't download if already downloaded
if [ ! -e qttools-opensource-src-5.7.1.7z ]
then
	wget --no-check-certificate http://download.qt-project.org/archive/qt/5.7/5.7.1/submodules/qttools-opensource-src-5.7.1.7z -O "$DEPS_ROOT/qttools-opensource-src-5.7.1.7z"
	# Verify downloaded file's hash (provided on Qt site)
	# v5.3.2 sha256=e3d026c8bd48ad41c3eec8e45ee4a3b9e39475438ce45dde90dc010164fc95c9
	# v5.7.1 sha256=904b1fb861c5b4923c0db1f1c1a06528a7c45c2170e13ca3eafbe10a54c366b4
	check_hash 904b1fb861c5b4923c0db1f1c1a06528a7c45c2170e13ca3eafbe10a54c366b4 "$DEPS_ROOT/qttools-opensource-src-5.7.1.7z"
fi
# don't extract if already extracted
cd "$PATH_DEPS/Qt"
if [ ! -d "qttools-opensource-src-5.7.1" ]
then
	cd "$DEPS_ROOT"
	"$CMD_7ZIP" x qttools-opensource-src-5.7.1.7z -aoa -o"$PATH_DEPS/Qt"
fi

cd "$DEPS_ROOT"
# don't download if already downloaded
if [ ! -e qtbase-opensource-src-5.7.1.7z ]
then
	wget --no-check-certificate http://download.qt-project.org/archive/qt/5.7/5.7.1/submodules/qtbase-opensource-src-5.7.1.7z -O "$DEPS_ROOT/qtbase-opensource-src-5.7.1.7z"
	# Verify downloaded file's hash (provided on Qt site)
	# v5.3.2 sha256=e7898f6a3f1b1ae19df120cbb1bf811b9699e441162c67ede0e97118093e6a7e
	# v5.7.1 sha256=a160e9c1403204f6a5c3a921de8530c3ff2e64f6608f259716f35f830e77f4d0
	check_hash a160e9c1403204f6a5c3a921de8530c3ff2e64f6608f259716f35f830e77f4d0 "$DEPS_ROOT/qtbase-opensource-src-5.7.1.7z"
fi
# don't extract if already extracted
cd "$PATH_DEPS/Qt"
if [ ! -d "5.7.1" ]
then
	cd "$DEPS_ROOT"
	"$CMD_7ZIP" x qtbase-opensource-src-5.7.1.7z -aoa -o"$PATH_DEPS/Qt"
	cd "$PATH_DEPS/Qt"
	mv qtbase-opensource-src-5.7.1 5.7.1
fi

# Qt 5.7.1 needs a configuration change for static library builds
cd "$PATH_DEPS/Qt/5.7.1/mkspecs/features"
patch --forward ./default_post.prf "$BITCOIN_GIT_ROOT/depends/patches/qt/fix_qt571_win32_qmake.patch"
cd "$DEPS_ROOT"

