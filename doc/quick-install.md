Installing BU from sources and from Ubuntu repos
======================================

Really quick guide to get your bitcoind/qt up and running


Other than the PPA repo mentioned above, serving stable version of Bitcoin Unlimited, we have set up another repository which will contain binaries built from source code snapshots of the BU development branch (`0.12.1bu` currently). If you're interested in testing the latest features included in BU but still not released just add this repository to your system. Use these commands:

```sh
sudo add-apt-repository ppa:bitcoin-unlimited/bu-ppa-nightly
sudo apt-get update
sudo apt-get install bitcoind bitcoin-qt
```
In case you installed both repos on your system, take into account that binaries belonging to the `nightly` will supersede the ones of the stable one.



