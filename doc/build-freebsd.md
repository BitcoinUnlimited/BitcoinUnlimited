FreeBSD build guide
======================
(updated for FreeBSD 9.3)

This guide describes how to build bitcoind and command-line utilities on FreeBSD.

As FreeBSD is most common as a server OS, we will not bother with the GUI.

Preparation
-------------

Run the following as root to install the base dependencies for building:

```bash
pkg install ca_root_nss autotools pkgconf gmake boost-libs openssl db48 git
```

The default C++ compiler that comes with FreeBSD 9.3 is g++ 4.2. This version is old (from 2007), and is not able to compile the current version of BitcoinUnlimited. It is possible to patch it up to compile, but we will instead use clang.


### Building BitcoinUnlimited

**Important**: use `gmake`, not `make`. The non-GNU `make` will exit with a horrible error.

Preparation:
```bash
export CC=clang
export CXX=clang++
export CXXFLAGS="-I/usr/local/include -I/usr/local/include/db48"
export LDFLAGS="-L/usr/local/lib -L/usr/local/lib/db48"
./autogen.sh
```

To configure without miniupnpc:
```bash
./configure --with-gui=no --without-miniupnpc
```

To configure with wallet:
```bash
./configure --with-gui=no
```

To configure without wallet:
```bash
./configure --disable-wallet --with-gui=no
```

Build and run the tests:
```bash
gmake
gmake check
```

Build faster by using 4 jobs:
```bash
gmake -j4
```
