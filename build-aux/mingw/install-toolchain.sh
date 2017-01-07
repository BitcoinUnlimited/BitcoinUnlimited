#!/bin/sh

#Convert paths from Windows style to POSIX style
DEPS_ROOT=$(echo "/$DEPS_ROOT" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
CMD_7ZIP=$(echo "/$CMD_7ZIP" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')
#Just put the toolchains in the MinGW root to keep everything in one place
TOOLCHAIN_ROOT=$(echo "/$MINGW_ROOT" | sed -e 's/\\/\//g' -e 's/://' -e 's/\"//g')

# Install required msys shell packages
mingw-get install msys-autoconf-bin
mingw-get install msys-automake-bin
mingw-get install msys-libtool-bin
mingw-get install msys-wget-bin

# Ensure dependency directory exists
mkdir -p "$DEPS_ROOT"

# Only install 32-bit tool chain if install path is provided
if [ -n "$BUILD_32_BIT" ]
then

	# Install the Mingw-64 toolchain (for 32-bit builds)
	# don't download if already downloaded
	DOWNLOAD_32="$DEPS_ROOT/toolchain_x86.7z"
	if [ ! -e "$DOWNLOAD_32" ]
	then
		wget --no-check-certificate http://sourceforge.net/projects/mingw-w64/files/Toolchains%20targetting%20Win32/Personal%20Builds/mingw-builds/4.9.2/threads-posix/dwarf/i686-4.9.2-release-posix-dwarf-rt_v3-rev1.7z/download --output-document="$DOWNLOAD_32"
	fi
	# don't extract if already extracted
	cd "$TOOLCHAIN_ROOT"
	if [ ! -d mingw32 ]
	then
		"$CMD_7ZIP" x "$DOWNLOAD_32" -aoa -o"$TOOLCHAIN_ROOT"
	fi
fi

# Only install 64-bit tool chain if specified for inclusion by user
if [ -n "$BUILD_64_BIT" ]
then
	# Install the Mingw-64 toolchain (for 64-bit builds)
	# don't download if already downloaded
	DOWNLOAD_64="$DEPS_ROOT/toolchain_x64.7z"
	if [ ! -e "$DOWNLOAD_64" ]
	then
		wget --no-check-certificate http://sourceforge.net/projects/mingw-w64/files/Toolchains%20targetting%20Win64/Personal%20Builds/mingw-builds/4.9.2/threads-posix/seh/x86_64-4.9.2-release-posix-seh-rt_v3-rev1.7z/download --output-document="$DOWNLOAD_64"
	fi
	# don't extract if already extracted
	cd "$TOOLCHAIN_ROOT"
	if [ ! -d mingw64 ]
	then
		"$CMD_7ZIP" x "$DOWNLOAD_64" -aoa -o"$TOOLCHAIN_ROOT"
	fi
fi
