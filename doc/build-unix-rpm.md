# UNIX BUILD NOTES (RPM)

Some notes on how to build Bitcoin Unlimited in Unix. Mostly with CentOS / RHEL focus.

For apt (Debian / Ubuntu) based distros, see [build-unix.md](build-unix.md).
For OpenBSD specific instructions, see [build-openbsd.md](build-openbsd.md)

## Note

Always use absolute paths to configure and compile bitcoin and the dependencies,
for example, when specifying the path of the dependency:

```bash
../dist/configure --enable-cxx --disable-shared --with-pic --prefix=$BDB_PREFIX
```

Here BDB_PREFIX must absolute path - it is defined using $(pwd) which ensures
the usage of the absolute path.

## To Build

```bash
git clone https://github.com/BitcoinUnlimited/BitcoinUnlimited.git
cd BitcoinUnlimited
./autogen.sh
./configure
make
make install # optional
```

This will build bitcoin-qt as well if the dependencies are met.

## Dependencies

These dependencies are required:

 Library     | Purpose          | Description
 ------------|------------------|----------------------
 libssl      | Crypto           | Random Number Generation, Elliptic Curve Cryptography
 libboost    | Utility          | Library for threading, data structures, etc
 libevent    | Networking       | OS independent asynchronous networking

Optional dependencies:

 Library     | Purpose          | Description
 ------------|------------------|----------------------
 miniupnpc   | UPnP Support     | Firewall-jumping support
 libdb4.8    | Berkeley DB      | Wallet storage (only needed when wallet enabled)
 qt          | GUI              | GUI toolkit (only needed when GUI enabled)
 protobuf    | Payments in GUI  | Data interchange format used for payment protocol (only needed when GUI enabled)
 libqrencode | QR codes in GUI  | Optional for generating QR codes (only needed when GUI enabled)
 libzmq3     | ZMQ notification | Optional, allows generating ZMQ notifications (requires ZMQ version >= 4.x)

For the versions used, see [dependencies.md](dependencies.md)

## System requirements

C++ compilers are memory-hungry. It is recommended to have at least 1 GB of
memory available when compiling Bitcoin Unlimited. With 512MB of memory or less
compilation will take much longer due to swap thrashing.

## Dependency Build Instructions: CentOS & RHEL

Build requirements:

## NOTE: you do not need to get boost from this source, but you do need at least boost 1.55 and CentOS7 by default has boost 1.53. This was the only place I could find a higher version of boost for CentOS7 that avoided compiling from source. If you do not trust it feel free to compile boost from source from the official boost github.
```bash
sudo yum groupinstall development
sudo yum install https://centos7.iuscommunity.org/ius-release.rpm
sudo yum install centos-release-scl
sudo yum install http://repo.okay.com.mx/centos/7/x86_64/release/okay-release-1-1.noarch.rpm
sudo yum install boost166-devel
sudo yum install libtool libevent-devel autoconf automake openssl-devel python36u libdb4-devel libdb4-cxx-devel
sudo yum install devtoolset-7-gcc*
sudo scl enable devtoolset-7 bash
```

Create/update symlink for python3 if needed:
```bash
sudo ln -fs /usr/bin/python3.6 /usr/bin/python3
```

An error for invalid python syntax when creating xversionkeys.h might occur. The fix is to update your default python to python3:
```bash
sudo ln -fs /usr/bin/python3 /usr/bin/python
```

To revert the default python version back to python2:
```bash
sudo ln -fs /user/bin/python2 /usr/bin/python
```


See the section "Disable-wallet mode" to build Bitcoin Unlimited without wallet.

Optional:

```bash
sudo yum install miniupnpc-devel (see --with-miniupnpc and --enable-upnp-default)
```

ZMQ dependencies:

```bash
sudo yum install zeromq-devel (provides ZMQ API 4.x)
```

## Dependencies for the GUI: CentOS & RHEL

If you want to build Bitcoin-Qt, make sure that the required packages for Qt development
are installed. Qt 5.3 or higher is necessary to build the GUI (QT 4 is not supported).
To build without GUI pass `--without-gui`.

To build with Qt 5.3 or higher you need the following:

```bash
sudo yum install qt5-qttools-devel qt5-qtbase-devel protobuf-devel
```

libqrencode (optional) can be installed with:

```bash
sudo yum install qrencode-devel
```

Once these are installed, they will be found by configure and a bitcoin-qt executable will be
built by default.

## Notes

The release is built with GCC and then "strip bitcoind" to strip the debug
symbols, which reduces the executable size by about 90%.


## miniupnpc

[miniupnpc](http://miniupnp.free.fr/) may be used for UPnP port mapping.  It can be downloaded from [here](
http://miniupnp.tuxfamily.org/files/).  UPnP support is compiled in and
turned off by default.  See the configure options for upnp behavior desired:

```bash
--without-miniupnpc      #No UPnP support miniupnp not required
--disable-upnp-default   #(the default) UPnP support turned off by default at runtime
--enable-upnp-default    #UPnP support turned on by default at runtime
```


## Berkeley DB

It is recommended to use Berkeley DB 4.8. If you have to build it yourself:

```bash
BITCOIN_ROOT=$(pwd)

# Pick some path to install BDB to, here we create a directory within the bitcoin directory
BDB_PREFIX="${BITCOIN_ROOT}/db4"
mkdir -p $BDB_PREFIX

# Fetch the source and verify that it is not tampered with
wget 'https://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz'
echo '12edc0df75bf9abd7f82f821795bcee50f42cb2e5f76a6a281b85732798364ef  db-4.8.30.NC.tar.gz' | sha256sum -c
# -> db-4.8.30.NC.tar.gz: OK
tar -xzvf db-4.8.30.NC.tar.gz

# Build the library and install to our prefix
cd db-4.8.30.NC/build_unix/
#  Note: Do a static build so that it can be embedded into the executable, instead of having to find a .so at runtime
../dist/configure --enable-cxx --disable-shared --with-pic --prefix=$BDB_PREFIX
make install

# Configure Bitcoin Unlimited to use our own-built instance of BDB
cd $BITCOIN_ROOT
./autogen.sh
./configure LDFLAGS="-L${BDB_PREFIX}/lib/" CPPFLAGS="-I${BDB_PREFIX}/include/" # (other args...)
```

**Note**: You only need Berkeley DB if the wallet is enabled (see the section *Disable-Wallet mode* below).

## Boost

If you need to build Boost yourself:
```bash
sudo su
./bootstrap.sh
./bjam install
```

## Security

To help make your Bitcoin installation more secure by making certain attacks impossible to
exploit even if a vulnerability is found, binaries are hardened by default.
This can be disabled with:

Hardening Flags:

```bash
./configure --enable-hardening
./configure --disable-hardening
```


Hardening enables the following features:

* Position Independent Executable
    Build position independent code to take advantage of Address Space Layout Randomization
    offered by some kernels. Attackers who can cause execution of code at an arbitrary memory
    location are thwarted if they don't know where anything useful is located.
    The stack and heap are randomly located by default but this allows the code section to be
    randomly located as well.

    On an AMD64 processor where a library was not compiled with -fPIC, this will cause an error
    such as: "relocation R_X86_64_32 against `......' can not be used when making a shared object;"

    To test that you have built PIE executable, install scanelf, part of paxutils, and use:

```bash
scanelf -e ./bitcoin
```

    The output should contain:

     TYPE
    ET_DYN

* Non-executable Stack
    If the stack is executable then trivial stack based buffer overflow exploits are possible if
    vulnerable buffers are found. By default, bitcoin should be built with a non-executable stack
    but if one of the libraries it uses asks for an executable stack or someone makes a mistake
    and uses a compiler extension which requires an executable stack, it will silently build an
    executable without the non-executable stack protection.

    To verify that the stack is non-executable after compiling use:
    `scanelf -e ./bitcoin`

    the output should contain:
	STK/REL/PTL
	RW- R-- RW-

    The STK RW- means that the stack is readable and writeable but not executable.

## Disable-wallet mode

When the intention is to run only a P2P node without a wallet, bitcoin may be compiled in
disable-wallet mode with:

```bash
./configure --disable-wallet
```

In this case there is no dependency on Berkeley DB 4.8.

Mining is also possible in disable-wallet mode, but only using the `getblocktemplate` RPC
call not `getwork`.

## Additional Configure Flags

A list of additional configure flags can be displayed with:

```bash
./configure --help
```

## Produce Static Binaries

If you want to build statically linked binaries so that you could compile in one machine
and deploy in same parch/platform boxes without the need of installing all the dependencies
just follow these steps:

```bash
git clone https://github.com/BitcoinUnlimited/BitcoinUnlimited.git BU
cd BU/depends
make HOST=x86_64-pc-linux-gnu NO_QT=1 -j4
cd ..
./configure --prefix=$PWD/depends/x86_64-pc-linux-gnu --without-gui
make -j4
```

in the above commands we are statically compiling headless 64 bit Linux binaries. If you want to compile
32 bit binaries just use `i686-pc-linux-gnu` rather than `x86_64-pc-linux-gnu`

For further documentation on the depends system see [README.md](../depends/README.md) in the depends directory.
