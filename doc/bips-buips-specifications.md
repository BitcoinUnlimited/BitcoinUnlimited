BIPs that are implemented by Bitcoin Unlimited (up-to-date up to **v1.3.0.1**):

* [`BIP 11`](https://github.com/bitcoin/bips/blob/master/bip-0011.mediawiki): Multisig outputs are standard since **v0.6.0** ([PR #669](https://github.com/bitcoin/bitcoin/pull/669)).
* [`BIP 13`](https://github.com/bitcoin/bips/blob/master/bip-0013.mediawiki): The address format for P2SH addresses has been implemented since **v0.6.0** ([PR #669](https://github.com/bitcoin/bitcoin/pull/669)).
* [`BIP 14`](https://github.com/bitcoin/bips/blob/master/bip-0014.mediawiki): The subversion string is being used as User Agent since **v0.6.0** ([PR #669](https://github.com/bitcoin/bitcoin/pull/669)).
* [`BIP 16`](https://github.com/bitcoin/bips/blob/master/bip-0016.mediawiki): The pay-to-script-hash evaluation rules have been implemented since **v0.6.0**, and took effect on *April 1st 2012* ([PR #748](https://github.com/bitcoin/bitcoin/pull/748)).
* [`BIP 21`](https://github.com/bitcoin/bips/blob/master/bip-0021.mediawiki): The URI format for Bitcoin payments has been implemented since **v0.6.0** ([PR #176](https://github.com/bitcoin/bitcoin/pull/176)).
* [`BIP 22`](https://github.com/bitcoin/bips/blob/master/bip-0022.mediawiki): The 'getblocktemplate' (GBT) RPC protocol for mining has been implemented since **v0.7.0** ([PR #936](https://github.com/bitcoin/bitcoin/pull/936)).
* [`BIP 23`](https://github.com/bitcoin/bips/blob/master/bip-0023.mediawiki): Some extensions to GBT have been implemented since **v0.10.0rc1**, including longpolling and block proposals ([PR #1816](https://github.com/bitcoin/bitcoin/pull/1816)).
* [`BIP 30`](https://github.com/bitcoin/bips/blob/master/bip-0030.mediawiki): The evaluation rules to forbid creating new transactions with the same txid as previous not-fully-spent transactions were implemented since **v0.6.0**, and the rule took effect on *March 15th 2012* ([PR #915](https://github.com/bitcoin/bitcoin/pull/915)).
* [`BIP 31`](https://github.com/bitcoin/bips/blob/master/bip-0031.mediawiki): The 'pong' protocol message (and the protocol version bump to 60001) has been implemented since **v0.6.1** ([PR #1081](https://github.com/bitcoin/bitcoin/pull/1081)).
* [`BIP 34`](https://github.com/bitcoin/bips/blob/master/bip-0034.mediawiki): The rule that requires blocks to contain their height (number) in the coinbase input, and the introduction of version 2 blocks has been implemented since **v0.7.0**. The rule took effect for version 2 blocks as of *block 224413* (March 5th 2013), and version 1 blocks are no longer allowed since *block 227931* (March 25th 2013) ([PR #1526](https://github.com/bitcoin/bitcoin/pull/1526)).
* [`BIP 35`](https://github.com/bitcoin/bips/blob/master/bip-0035.mediawiki): The 'mempool' protocol message (and the protocol version bump to 60002) has been implemented since **v0.7.0** ([PR #1641](https://github.com/bitcoin/bitcoin/pull/1641)).
* [`BIP 37`](https://github.com/bitcoin/bips/blob/master/bip-0037.mediawiki): The bloom filtering for transaction relaying, partial merkle trees for blocks, and the protocol version bump to 70001 (enabling low-bandwidth SPV clients) has been implemented since **v0.8.0** ([PR #1795](https://github.com/bitcoin/bitcoin/pull/1795)).
* [`BIP 42`](https://github.com/bitcoin/bips/blob/master/bip-0042.mediawiki): The bug that would have caused the subsidy schedule to resume after block 13440000 was fixed in **v0.9.2** ([PR #3842](https://github.com/bitcoin/bitcoin/pull/3842)).
* [`BIP 61`](https://github.com/bitcoin/bips/blob/master/bip-0061.mediawiki): The 'reject' protocol message (and the protocol version bump to 70002) was added in **v0.9.0** ([PR #3185](https://github.com/bitcoin/bitcoin/pull/3185)).
* [`BIP 65`](https://github.com/bitcoin/bips/blob/master/bip-0065.mediawiki): The CHECKLOCKTIMEVERIFY softfork was merged in **v0.12.0** ([PR #6351](https://github.com/bitcoin/bitcoin/pull/6351)), and backported to **v0.11.2** and **v0.10.4**. Mempool-only CLTV was added in [PR #6124](https://github.com/bitcoin/bitcoin/pull/6124).
* [`BIP 66`](https://github.com/bitcoin/bips/blob/master/bip-0066.mediawiki): The strict DER rules and associated version 3 blocks have been implemented since **v0.10.0** ([PR #5713](https://github.com/bitcoin/bitcoin/pull/5713)).
* [`BIP 70`](https://github.com/bitcoin/bips/blob/master/bip-0070.mediawiki) [`71`](https://github.com/bitcoin/bips/blob/master/bip-0071.mediawiki) [`72`](https://github.com/bitcoin/bips/blob/master/bip-0072.mediawiki): Payment Protocol support has been available in Bitcoin Unlimited GUI since **v0.9.0** ([PR #5216](https://github.com/bitcoin/bitcoin/pull/5216)).
* [`BIP 111`](https://github.com/bitcoin/bips/blob/master/bip-0111.mediawiki): `NODE_BLOOM` service bit added, and enforced for all peer versions as of **v1.1.0** ([PR #6579](https://github.com/bitcoin/bitcoin/pull/6579) and [PR #6641](https://github.com/bitcoin/bitcoin/pull/6641)).
* [`BIP 130`](https://github.com/bitcoin/bips/blob/master/bip-0130.mediawiki): direct headers announcement is negotiated with peer versions `>=70012` as of **v0.12.0** ([PR 6494](https://github.com/bitcoin/bitcoin/pull/6494)).

BIPs that are implemented by Bitcoin Unlimited (up-to-date up to dev@43aa7a05):

* [`BUIP 010`](https://github.com/BitcoinUnlimited/BUIP/blob/master/010.mediawiki): Xtreme Thinblocks (initial commit: 2c9e2c62f5bf4369f4bff7ba783ae32432de4f68 Fri Jan 15 21:37:25 2016 -0800)
* [`BUIP 018`](https://github.com/BitcoinUnlimited/BUIP/blob/master/018.mediawiki): Bitnodes Seeding and User-Configurable DNS Seeds ([PR #30](https://github.com/BitcoinUnlimited/BitcoinUnlimited/pull/30)).
* [`BUIP 033`](https://github.com/BitcoinUnlimited/BUIP/blob/master/018.mediawiki): Parallel Block Validation ([PR #254](https://github.com/BitcoinUnlimited/BitcoinUnlimited/pull/254)).
* [`BUIP 070`](https://github.com/BitcoinUnlimited/BUIP/blob/master/070.mediawiki): Support BitPay's new Bitcoin Cash address format in BUCash ([PR #883](https://github.com/BitcoinUnlimited/BitcoinUnlimited/pull/883)).
* [`BUIP 085`](https://github.com/BitcoinUnlimited/BUIP/blob/master/085.mediawiki): Double spend relaying ([PR #1109](https://github.com/BitcoinUnlimited/BitcoinUnlimited/pull/1109)).

Bitcoin Cash Specification (up-to-date up to **v1.3.0.1**):

* [August 1st 2017 Protocol Upgrade](https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/uahf-technical-spec.md)
* [New Bitcoin Cash Address Format](https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/cashaddr.md)
* [May 15th 2018 Protocol Upgrade](https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/may-2018-hardfork.md)
