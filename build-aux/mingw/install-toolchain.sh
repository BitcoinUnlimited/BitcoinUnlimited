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

# Install required msys shell packages
mingw-get install msys-autoconf-bin
mingw-get install msys-automake-bin
mingw-get install msys-libtool-bin
mingw-get install msys-wget-bin

# Ensure dependency directory exists
mkdir -p $PATH_DEPS
cd $PATH_DEPS

# Strip the /bin sub directory
TOOLCHAIN_ROOT=${TOOLCHAIN_BIN%/*}
# Strip the /mingw32 sub directory
TOOLCHAIN_ROOT=${TOOLCHAIN_ROOT%/*}

# Install the Mingw-64 toolchain
# don't download if already downloaded
if [ ! -e toolchain.7z ]
then
	wget --no-check-certificate http://sourceforge.net/projects/mingw-w64/files/Toolchains%20targetting%20Win32/Personal%20Builds/mingw-builds/4.9.2/threads-posix/dwarf/i686-4.9.2-release-posix-dwarf-rt_v3-rev1.7z/download --output-document=$PATH_DEPS/toolchain.7z
fi
# don't extract if already extracted
cd $TOOLCHAIN_ROOT
if [ ! -d mingw32 ]
then
	cd $PATH_DEPS
	"$CMD_7ZIP" x toolchain.7z -aoa -o$TOOLCHAIN_ROOT
fi
