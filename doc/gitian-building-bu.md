Gitian build
============

This is guide take for granted that you are using Ubuntu bionic 18.04 as host OS. the aim of the document is to be able to produce deterministic binaries using gitian-tools and docker containers.


Prerequisite
-------------

These are steps that as to be executed once and that don't need to be repeated for every new gitian build process.

```bash
sudo apt install git apt-cacher-ng ruby docker.io
sudo usermod -a -G docker $USER
cd ~/src
git clone https://github.com/BitcoinUnlimited/BitcoinUnlimited.git
git clone https://github.com/devrandom/gitian-builder.git
cd gitian-builder
bin/make-base-vm --suite bionic --arch amd64 --docker
```

Build the binaries
------------------

cd ~/src/gitian-builder
export USE_DOCKER=1
bin/gbuild -j 4 -m 10000 --url bitcoin=https://github.com/BitcoinUnlimited/BitcoinUnlimited.git --commit bitcoin=dev ../BitcoinUnlimited/contrib/gitian-descriptors/gitian-linux.yml

Your binaries will be ready to be used in `build/out/` folder.

