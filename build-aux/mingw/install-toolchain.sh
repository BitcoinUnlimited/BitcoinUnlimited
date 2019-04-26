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

# Dot (.) notation version compare pulled from https://stackoverflow.com/a/4025065
vercomp () {
    if [[ $1 == $2 ]]
    then
        return 0
    fi
    local IFS=.
    local i ver1=($1) ver2=($2)
    # fill empty fields in ver1 with zeros
    for ((i=${#ver1[@]}; i<${#ver2[@]}; i++))
    do
        ver1[i]=0
    done
    for ((i=0; i<${#ver1[@]}; i++))
    do
        if [[ -z ${ver2[i]} ]]
        then
            # fill empty fields in ver2 with zeros
            ver2[i]=0
        fi
        if ((10#${ver1[i]} > 10#${ver2[i]}))
        then
            return 1
        fi
        if ((10#${ver1[i]} < 10#${ver2[i]}))
        then
            return 2
        fi
    done
    return 0
}

# Install required msys shell packages
mingw-get install msys-autoconf-bin
mingw-get install msys-automake-bin
mingw-get install msys-libtool-bin
# NOTE: This is a very old version of wget (v1.12) and does not support TLSv1.2
#       so we will only use this version to download the latest version v1.20.3
mingw-get install msys-wget-bin


# Ensure dependency directory exists
mkdir -p "$DEPS_ROOT"
cd "$DEPS_ROOT"

# Check to see if we need to update WGet.  TLS v1.2 support wasn't added until v1.16.1
# NOTE: TLSv1.3 support added in v1.20
WGET_VERSION=$(wget --version | sed -nre 's/^GNU Wget [^0-9]*(([0-9]+\.)*[0-9]+).*/\1/p')
vercomp $WGET_VERSION "1.16.1"
case $? in
	0) MUST_UPGRADE_WGET=0;;
	1) MUST_UPGRADE_WGET=0;;
	2) MUST_UPGRADE_WGET=1;;
esac

if [ "$MUST_UPGRADE_WGET" -eq "1" ]
then
    echo "Upgrading WGet from version $WGET_VERSION"
	# Use the v1.12 wget client to download & install the v1.20 version
	# don't download if already downloaded
	if [ ! -e wget-1.20.3-win32.zip ]
	then
		wget --no-check-certificate https://eternallybored.org/misc/wget/releases/wget-1.20.3-win32.zip -O "$DEPS_ROOT/wget-1.20.3-win32.zip"
		# Verify downloaded file's hash
		# NOTE: This hash was self computed as it was not provided by the author
		# v1.19.4 win32 sha256=b1a7e4ba4ab7f78e588c1186f2a5d7e1726628a5a66c645e41f8105b7cf5f61c
		# v1.20.3 win32 sha256=021F547BACA74FCA939D50951CE967502D160A7502F02FAB706F9293E1475FB8
		check_hash 021F547BACA74FCA939D50951CE967502D160A7502F02FAB706F9293E1475FB8 "$DEPS_ROOT/wget-1.20.3-win32.zip"
	fi
	# don't extract if already extracted
	if [ ! -d wget-1.20.3-win32 ]
	then
		"$CMD_7ZIP" x wget-1.20.3-win32.zip -aoa -o"$DEPS_ROOT/wget-1.20.3-win32"
		cd "$DEPS_ROOT/wget-1.20.3-win32"
		cp wget.exe "$MSYS_BIN/wget.exe"
	fi
fi

# Verify that we have a TLS v1.2 supporting version, if not fail out now.
WGET_VERSION=$(wget --version | sed -nre 's/^GNU Wget [^0-9]*(([0-9]+\.)*[0-9]+).*/\1/p')
vercomp $WGET_VERSION "1.16.1"
case $? in
	0) MUST_UPGRADE_WGET=0;;
	1) MUST_UPGRADE_WGET=0;;
	2) MUST_UPGRADE_WGET=1;;
esac

if [ "$MUST_UPGRADE_WGET" -eq "1" ]
then
	echo ""
	echo "ERROR: Unable to upgrade wget to a version that supports TLS v1.2"
	echo "       At a minimum, version 1.16.1 is required, though it is reccommended to install the latest version."
	echo "       You may need to manually download and install wget to proceed."
	echo "       Please visit https://eternallybored.org/misc/wget and download the latest 32-bit release version."
	echo "       The wget.exe file should be placed in $MSYS_BIN overwriting the existing version (if present)."
	exit -5
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
		wget --no-check-certificate https://sourceforge.net/projects/mingw-w64/files/Toolchains%20targetting%20Win32/Personal%20Builds/mingw-builds/7.3.0/threads-posix/dwarf/i686-7.3.0-release-posix-dwarf-rt_v5-rev0.7z/download --output-document="$DOWNLOAD_32"
		# Verify downloaded file's hash
		# v4.9.2 v3 rev1 sha1=a315254e0e85cfa170939e8c6890a7df1dc6bd20
		# v7.3.0 v5 rev0 sha1=96e11c754b379c093e1cb3133f71db5b9f3e0532
		# NOTE: The sha256 has was self computed, but the sha1 provided by the publisher was verified first
		# v4.9.2 v3 rev1 sha256=f6de32350a28f4b6c30eec26dbfee65f112300d51e37e4d2007b0598bef9bb79
		# v7.3.0 v5 rev0 sha256=0475B097AD645AE25438AE3470AF7E16E218EC1BD617B73E50B6A6C9622589A7
		check_hash 0475B097AD645AE25438AE3470AF7E16E218EC1BD617B73E50B6A6C9622589A7 "$DOWNLOAD_32"
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
		wget --no-check-certificate https://sourceforge.net/projects/mingw-w64/files/Toolchains%20targetting%20Win64/Personal%20Builds/mingw-builds/7.3.0/threads-posix/seh/x86_64-7.3.0-release-posix-seh-rt_v5-rev0.7z/download --output-document="$DOWNLOAD_64"
		# Verify downloaded file's hash
		# v4.9.2 v3 rev1 sha1=c160858ddba88110077c9f853a38b254ca0bdb1b
		# v7.3.0 v5 rev0 sha1=0fce15036400568babd10d65b247e9576515da2c
		# NOTE: The sha256 has was self computed, but the sha1 provided by the publisher was verified first
		# v4.9.2 v3 rev1 sha256=58626ce6d93199784ef7fe73790ebbdbf5a157be8d30ae396d437748e69c0cf3
		# v7.3.0 v5 rev0 sha256=784D25B00E7CF27AA64ABE2363B315400C27526BFCE672FDEE97137F71823D03
		check_hash 784D25B00E7CF27AA64ABE2363B315400C27526BFCE672FDEE97137F71823D03 "$DOWNLOAD_64"
	fi
	# don't extract if already extracted
	cd "$TOOLCHAIN_ROOT"
	if [ ! -d mingw64 ]
	then
		"$CMD_7ZIP" x "$DOWNLOAD_64" -aoa -o"$TOOLCHAIN_ROOT"
	fi
fi
