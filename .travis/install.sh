#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Copyright (c) 2019 The BitcoinUnlimited Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

if [ $DIST != "RPM" ]; then
  export LC_ALL=C.UTF-8
fi

travis_retry docker pull "$DOCKER_NAME_TAG"
env | grep -E '^(CCACHE_|WINEDEBUG|LC_ALL|BOOST_TEST_RANDOM|CONFIG_SHELL)' | tee /tmp/env
if [[ $HOST = *-mingw32 ]]; then
  DOCKER_ADMIN="--cap-add SYS_ADMIN";
fi
DOCKER_ID=$(docker run $DOCKER_ADMIN -idt --mount type=bind,src=$TRAVIS_BUILD_DIR,dst=$TRAVIS_BUILD_DIR --mount type=bind,src=$CCACHE_DIR,dst=$CCACHE_DIR -w $TRAVIS_BUILD_DIR --env-file /tmp/env $DOCKER_NAME_TAG)
DOCKER_EXEC () {
  docker exec $DOCKER_ID bash -c "cd $PWD && $*";
}
if [ -n "$DPKG_ADD_ARCH" ]; then
  DOCKER_EXEC dpkg --add-architecture "$DPKG_ADD_ARCH";
fi
if [ $DIST = "DEB" ]; then
  travis_retry DOCKER_EXEC apt-get update
  travis_retry DOCKER_EXEC apt-get install --no-install-recommends --no-upgrade -qq $PACKAGES $DOCKER_PACKAGES
fi
if [ $DIST = "RPM" ]; then
  travis_retry DOCKER_EXEC yum update -y
  travis_retry DOCKER_EXEC yum groupinstall -y development
  travis_retry DOCKER_EXEC yum install -y https://centos7.iuscommunity.org/ius-release.rpm
  travis_retry DOCKER_EXEC yum install -y http://repo.okay.com.mx/centos/7/x86_64/release/okay-release-1-1.noarch.rpm
  travis_retry DOCKER_EXEC yum install -y boost166-devel
  # this is temporary until the default compiler on centos/rhel supports c++14
  travis_retry DOCKER_EXEC yum install -y centos-release-scl
  travis_retry DOCKER_EXEC yum install -y $PACKAGES $DOCKER_PACKAGES_RPM
  # this is temporary until the default compiler on centos/rhel supports c++14
  travis_retry DOCKER_EXEC scl enable devtoolset-7 bash
fi
if [ $RUN_FORMATTING_CHECK = "true" ]; then
  curl --location $LINTER_DEB_URL/libllvm3.8_3.8.1-27ubuntu1_amd64.deb -o llvm-3.8.deb;
  curl --location $LINTER_DEB_URL/clang-format-3.8_3.8.1-27ubuntu1_amd64.deb -o clang-format-3.8.deb;
  DOCKER_EXEC dpkg -i llvm-3.8.deb clang-format-3.8.deb;
fi
