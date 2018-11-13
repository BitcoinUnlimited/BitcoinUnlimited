FROM ubuntu:18.04

RUN apt-get update
RUN apt-get install -y curl libdb-dev libdb++-dev build-essential libtool autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils libboost-all-dev libminiupnpc-dev libzmq3-dev git unzip wget vim g++-mingw-w64-i686 mingw-w64-i686-dev

RUN apt-get clean && rm -rf /var/lib/apt/lists/*

# change the url to your forked repo if you dont want to pull the bitcoinunlimted repo
RUN git clone https://github.com/bitcoinunlimited/bitcoinunlimited

# to checkout a specific branch uncomment the lines in the section below
###############
# WORKDIR /bitcoinunlimited
# git checkout <branch name>
###############

WORKDIR /bitcoinunlimited/depends

RUN make HOST=i686-w64-mingw32

WORKDIR /bitcoinunlimited

RUN ./autogen.sh
RUN $PWD/depends/i686-w64-mingw32/share/config.site ./configure --enable-reduce-exports --with-gui=no --with-incompatible-bdb --prefix=/

RUN make -j2

RUN mkdir /root/.bitcoin/
