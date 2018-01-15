#!/bin/sh

#Convert paths from Windows style to POSIX style
MINGW_BIN=$(echo "/$MINGW_BIN" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
MSYS_BIN=$(echo "/$MSYS_BIN" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
DEPS_ROOT=$(echo "/$DEPS_ROOT" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
CMD_7ZIP=$(echo "/$CMD_7ZIP" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
#Just put the toolchains in the MinGW root to keep everything in one place
TOOLCHAIN_ROOT=$(echo "/$MINGW_ROOT" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')

# Set PATH using POSIX style paths
PATH="$MSYS_BIN:$MINGW_BIN:$PATH"

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


# Install required msys shell packages
mingw-get install msys-autoconf-bin
mingw-get install msys-automake-bin
mingw-get install msys-libtool-bin
# NOTE: This is a very old version of wget (v1.12) and does not support TLSv1.2
#       so we will only use this version to download the latest version v1.19.2
mingw-get install msys-wget-bin


# Ensure dependency directory exists
mkdir -p "$DEPS_ROOT"
cd "$DEPS_ROOT"

# Use the v1.12 wget client to download & install the v1.19 version
# don't download if already downloaded
if [ ! -e wget-1.19.1-win32.zip ]
then
	wget --no-check-certificate https://eternallybored.org/misc/wget/releases/old/wget-1.19.1-win32.zip -O "$DEPS_ROOT/wget-1.19.1-win32.zip"
	# Verify downloaded file's hash
	# NOTE: This hash was self computed as it was not provided by the author
	# v2016.04.08 sha256=eab20c797098c9e9a9753b2bdb530ed8758bbdb1f4a9a434bff48ea8840c5bee
	check_hash eab20c797098c9e9a9753b2bdb530ed8758bbdb1f4a9a434bff48ea8840c5bee "$DEPS_ROOT/wget-1.19.1-win32.zip"
fi
# don't extract if already extracted
if [ ! -d wget-1.19.1-win32 ]
then
	"$CMD_7ZIP" x wget-1.19.1-win32.zip -aoa -o"$DEPS_ROOT/wget-1.19.1-win32"
	cd "$DEPS_ROOT/wget-1.19.1-win32"
	cp wget.exe "$MSYS_BIN/wget.exe"
fi
#pause for debugging purposes
#read -rsp $'Press any key to continue...\n' -n 1 key



# Only install 32-bit tool chain if install path is provided
if [ -n "$BUILD_32_BIT" ]
then

	# Install the Mingw-64 toolchain (for 32-bit builds)
	# don't download if already downloaded
	DOWNLOAD_32="$DEPS_ROOT/toolchain_x86.7z"
	if [ ! -e "$DOWNLOAD_32" ]
	then
		wget --no-check-certificate http://sourceforge.net/projects/mingw-w64/files/Toolchains%20targetting%20Win32/Personal%20Builds/mingw-builds/4.9.2/threads-posix/dwarf/i686-4.9.2-release-posix-dwarf-rt_v3-rev1.7z/download --output-document="$DOWNLOAD_32"
		# Verify downloaded file's hash
		# sha1=a315254e0e85cfa170939e8c6890a7df1dc6bd20
		# NOTE: The sha256 has was self computed, but the sha1 provided by the publisher was verified first
		# v4.9.2 v3 rev1 sha256=f6de32350a28f4b6c30eec26dbfee65f112300d51e37e4d2007b0598bef9bb79
		check_hash f6de32350a28f4b6c30eec26dbfee65f112300d51e37e4d2007b0598bef9bb79 "$DOWNLOAD_32"
	fi
	# don't extract if already extracted
	cd "$TOOLCHAIN_ROOT"
	if [ ! -d mingw32 ]
	then
		"$CMD_7ZIP" x "$DOWNLOAD_32" -aoa -o"$TOOLCHAIN_ROOT"
	fi
fi

cd "$DEPS_ROOT"
# Only install 64-bit tool chain if specified for inclusion by user
if [ -n "$BUILD_64_BIT" ]
then
	# Install the Mingw-64 toolchain (for 64-bit builds)
	# don't download if already downloaded
	DOWNLOAD_64="$DEPS_ROOT/toolchain_x64.7z"
	if [ ! -e "$DOWNLOAD_64" ]
	then
		wget --no-check-certificate http://sourceforge.net/projects/mingw-w64/files/Toolchains%20targetting%20Win64/Personal%20Builds/mingw-builds/4.9.2/threads-posix/seh/x86_64-4.9.2-release-posix-seh-rt_v3-rev1.7z/download --output-document="$DOWNLOAD_64"
		# Verify downloaded file's hash
		# sha1=c160858ddba88110077c9f853a38b254ca0bdb1b
		# NOTE: The sha256 has was self computed, but the sha1 provided by the publisher was verified first
		# v4.9.2 v3 rev1 sha256=58626ce6d93199784ef7fe73790ebbdbf5a157be8d30ae396d437748e69c0cf3
		check_hash 58626ce6d93199784ef7fe73790ebbdbf5a157be8d30ae396d437748e69c0cf3 "$DOWNLOAD_64"
	fi
	# don't extract if already extracted
	cd "$TOOLCHAIN_ROOT"
	if [ ! -d mingw64 ]
	then
		"$CMD_7ZIP" x "$DOWNLOAD_64" -aoa -o"$TOOLCHAIN_ROOT"
	fi
fi
