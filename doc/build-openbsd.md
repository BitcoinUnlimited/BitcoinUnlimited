# OpenBSD build guide
(Tested with OpenBSD 6.6)

This guide describes how to build bitcoind and command-line utilities on OpenBSD.

As OpenBSD is most common as a server OS, we will not bother with the GUI.

**Important**: use `gmake`, not `make`. The non-GNU `make` will exit with a horrible error.

## Installing dependencies

Run the following as root to install the base dependencies for building:

```bash
pkg_add gmake libtool libevent git boost
pkg_add autoconf # (select highest version, e.g. 2.69)
pkg_add automake # (select highest version, e.g. 1.16)
pkg_add python # (select highest version, e.g. 3.7)
```
To install openSSL, find download link here https://www.openssl.org/source/.

Still as root (while modifying the version as apropriate) run

```bash
curl 'https://www.openssl.org/source/openssl-1.1.0l.tar.gz' -o openssl-1.1.0l.tar.gz
echo '74a2f756c64fd7386a29184dc0344f4831192d61dc2481a93a4c5dd727f41148 openssl-1.1.0l.tar.gz' | sha256 -c
# MUST output: (SHA256) openssl-1.1.0l.tar.gz: OK
tar xvzf openssl-1.1.0l.tar.gz
cd openssl-1.1.0l
./config
gmake install
```

See [dependencies.md](dependencies.md) for a complete overview.


## Building Bitcoin Unlimited

As your normal (non root) user, go through the steps below

### Fetch the code

```bash
git clone https://github.com/BitcoinUnlimited/BitcoinUnlimited.git
cd BitcoinUnlimited/
```

### Preparation

```bash
export AUTOCONF_VERSION=2.69 # replace this with the autoconf version that you installed
export AUTOMAKE_VERSION=1.16 # replace this with the automake version that you installed
export CC=cc
export CXX=c++
export MAKE=gmake
```

Now you need to choose to build without or with wallet functionality. If you just require a running node, you generally don't need wallet functionality

### To build without wallet

While in the `BitcoinUnlimited` directory

```bash
./autogen.sh
./configure --disable-wallet --with-gui=no
gmake # You may get an error with one of the tests.
```

You will find the `bitcoind` binary in the `src/` folder.


### To build with wallet

To stay backwards compatible with old walletfiles we specifically need BerkeleyDB 4.8.
You cannot use the BerkeleyDB library from ports.


#### Building BerkeleyDB

BerkeleyDB is only necessary for the wallet functionality. To skip this, pass `--disable-wallet` to `./configure`. See above.

To build Berkeley DB 4.8:

```bash
# Pick some path to install BDB to, here we create a directory within the bitcoin directory
BDB_PREFIX=$(pwd)/db4
mkdir -p $BDB_PREFIX

# Fetch the source and verify that it is not tampered with
curl 'https://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz' -o db-4.8.30.NC.tar.gz
echo '12edc0df75bf9abd7f82f821795bcee50f42cb2e5f76a6a281b85732798364ef  db-4.8.30.NC.tar.gz' | sha256 -c
# MUST output: (SHA256) db-4.8.30.NC.tar.gz: OK
tar -xzf db-4.8.30.NC.tar.gz

# Fetch, verify that it is not tampered with and apply clang related patch
cd db-4.8.30.NC/
curl 'https://gist.githubusercontent.com/LnL7/5153b251fd525fe15de69b67e63a6075/raw/7778e9364679093a32dec2908656738e16b6bdcb/clang.patch' -o clang.patch
echo '7a9a47b03fd5fb93a16ef42235fa9512db9b0829cfc3bdf90edd3ec1f44d637c  clang.patch' | sha256 -c
# MUST output: (SHA256) clang.patch: OK
patch -p2 < clang.patch

# Build the library and install to specified prefix
cd build_unix/
../dist/configure --enable-cxx --disable-shared --with-pic --prefix=$BDB_PREFIX
gmake install
cd ../..
```

You should now have the required files in `db4/lib/` and `db4/include/`.

### To build with wallet

Make sure `BDB_PREFIX` is set to the appropriate path from building BDB. See above.

While in the `BitcoinUnlimited` directory.

```bash
./autogen.sh
./configure LDFLAGS="-L${BDB_PREFIX}/lib/" CPPFLAGS="-I${BDB_PREFIX}/include/" --with-gui=no
gmake
```

