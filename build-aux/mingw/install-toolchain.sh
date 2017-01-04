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
    --path-toolchain-32=*)
    TOOLCHAIN_BIN_32="${i#*=}"
    shift # past argument=value
    ;;
    --path-toolchain-64=*)
    TOOLCHAIN_BIN_64="${i#*=}"
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

PATH="$MSYS_BIN:$MINGW_BIN:$PATH"

# Install required msys shell packages
mingw-get install msys-autoconf-bin
mingw-get install msys-automake-bin
mingw-get install msys-libtool-bin
mingw-get install msys-wget-bin

# Ensure dependency directory exists
mkdir -p "$PATH_DEPS"
cd "$PATH_DEPS"

# Only install 32-bit tool chain if install path is provided
if [ -n "$TOOLCHAIN_BIN_32" ]
then
	# Strip the /bin sub directory
	TOOLCHAIN_ROOT_32=${TOOLCHAIN_BIN_32%/*}
	# Strip the /mingw32 sub directory
	TOOLCHAIN_ROOT_32=${TOOLCHAIN_ROOT_32%/*}
	
	# Install the Mingw-64 toolchain (for 32-bit builds)
	# don't download if already downloaded
	if [ ! -e toolchain_x86.7z ]
	then
		wget --no-check-certificate http://sourceforge.net/projects/mingw-w64/files/Toolchains%20targetting%20Win32/Personal%20Builds/mingw-builds/4.9.2/threads-posix/dwarf/i686-4.9.2-release-posix-dwarf-rt_v3-rev1.7z/download --output-document="$PATH_DEPS/toolchain_x86.7z"
	fi
	# don't extract if already extracted
	cd "$TOOLCHAIN_ROOT_32"
	if [ ! -d mingw32 ]
	then
		cd "$PATH_DEPS"
		"$CMD_7ZIP" x toolchain_x86.7z -aoa -o"$TOOLCHAIN_ROOT_32"
	fi
fi

cd "$PATH_DEPS"
# Only install 64-bit tool chain if install path is provided
if [ -n "$TOOLCHAIN_BIN_64" ]
then
	# Strip the /bin sub directory
	TOOLCHAIN_ROOT_64=${TOOLCHAIN_BIN_64%/*}
	# Strip the /mingw64 sub directory
	TOOLCHAIN_ROOT_64=${TOOLCHAIN_ROOT_64%/*}
	
	# Install the Mingw-64 toolchain (for 64-bit builds)
	# don't download if already downloaded
	if [ ! -e toolchain_x64.7z ]
	then
		wget --no-check-certificate http://sourceforge.net/projects/mingw-w64/files/Toolchains%20targetting%20Win64/Personal%20Builds/mingw-builds/4.9.2/threads-posix/seh/x86_64-4.9.2-release-posix-seh-rt_v3-rev1.7z/download --output-document="$PATH_DEPS/toolchain_x64.7z"
	fi
	# don't extract if already extracted
	cd "$TOOLCHAIN_ROOT_64"
	if [ ! -d mingw64 ]
	then
		cd "$PATH_DEPS"
		"$CMD_7ZIP" x toolchain_x64.7z -aoa -o"$TOOLCHAIN_ROOT_64"
	fi
fi
