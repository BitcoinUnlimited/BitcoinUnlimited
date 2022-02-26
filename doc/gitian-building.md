Gitian build
============

This guide takes for granted that you are using Ubuntu Bionic 18.04 as host OS. The aim of the document is to be able to produce deterministic binaries using gitian-tools and docker containers.

Prerequisite
-------------

These are steps that as to be executed once and that don't need to be repeated for every new gitian build process.

```bash
sudo apt install git apt-cacher-ng ruby docker.io
sudo usermod -a -G docker $USER
exec su -l $USER  #make effective the usermod command
mkdir -p ~/src
cd ~/src
git clone https://github.com/BitcoinUnlimited/BitcoinUnlimited.git
git clone https://github.com/devrandom/gitian-builder.git
cd gitian-builder
bin/make-base-vm --suite bionic --arch amd64 --docker
```

Build the binaries
------------------

These are the commands to actually produce the linux 32/64 bit executables :

```bash
cd ~/src/gitian-builder
export USE_DOCKER=1
bin/gbuild -j 4 -m 10000 --url bitcoin=https://github.com/BitcoinUnlimited/BitcoinUnlimited.git --commit bitcoin=dev ../BitcoinUnlimited/contrib/gitian-descriptors/gitian-linux.yml
```

Your binaries will be ready to be used in `build/out/` folder.

To compile binaries for MacOSX darwin first you need to get OSX SDK from here https://github.com/phracker/MacOSX-SDKs/releases/download/11.3/MacOSX10.15.sdk.tar.xz

Then issue the following command to have the binaries for osx produced and stored in `build/out`


```bash
cd ~/src/gitian-builder
export USE_DOCKER=1
bin/gbuild -j 4 -m 10000 --url bitcoin=https://github.com/BitcoinUnlimited/BitcoinUnlimited.git --commit bitcoin=dev ../BitcoinUnlimited/contrib/gitian-descriptors/gitian-osx.yml
```
