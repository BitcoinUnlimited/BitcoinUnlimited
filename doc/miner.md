[Website](https://www.bitcoinunlimited.info)  | [Download](https://www.bitcoinunlimited.info/download) | [Setup](../README.md)   |   [Miner](miner.md)  |  [ElectronCash](bu-electrum-integration.md)  |  [UnconfirmedChains](unconfirmedTxChainLimits.md)

# Using Bitcoin Unlimited for Mining

Bitcoin Unlimited is based on the Satoshi codebase, so it is a drop in replacement for your mining pool software.  Simply configure your pool to point to the Bitcoin Unlimited daemon, in the exact same manner you would for the Bitcoin Core daemon.

But Bitcoin Unlimited has specific features to facilitate mining.

## ***getminingcandidate*** and ***submitminingsolution***

*efficient protocol to access block candidates and submit block solutions*


Bitcoin Unlimited provides 2 additional mining RPC functions that can be used instead of "getblocktemplate" and "submitblock".  These RPCs do not pass the entire block to mining pools.  Instead, the candidate block header, proposed coinbase transaction, and coinbase merkle proof are passed.  This is the approximately the same data that is passed to hashing hardware via the Stratum protocol, so if you are familiar with Stratum, you are familiar with how this is possible.

A mining pool uses ***getminingcandidate*** to receive the previously described block information and a tracking identifier.  It then may modify or completely replace the coinbase transaction and many block header fields, to create different candidates for hashing hardware.  It then forwards these candidates to the hashing hardware via Stratum.  When a solution is found, the mining pool can submit the solution back to bitcoind via ***submitminingsolution***.

A few of the benefits when using RPC getminingcandidate and RPC submitminingsolution are:
* Massively reduced bandwidth and latency, especially for large blocks.  This RPC requires log2(blocksize) data. 
* Faster JSON parsing and creation
* Concise JSON

### bitcoin-miner

An example CPU-miner program is provided that shows a proof-of-concept use of these functions.
The source code is located at bitcoin-miner.cpp.  To try it out, run
```sh
bitcoin-miner
```

Of course, given current and foreseeable mining difficulties this program will not find any blocks on mainnet.  However, it will find blocks on testnet or regtest.

### miningtest.py

A python based test of these interfaces is located at qa/rpc-tests/miningtest.py.  This example may be of more use for people accessing these RPCs in higher level languages.

### Function documentation:

#### RPC getminingcandidate

##### Arguments: -none
##### Returns:
```
{
  # candidate identifier for submitminingsolution (integer):
  "id": 14,
  
  # Hash of the previous block (hex string):
  "prevhash": "0000316517e048ab283a41df3c0ba125345a5c56ef3f76db901b0ede65e2f0e5",
  
  # Coinbase transaction (hex string encoded binary transaction)
  "coinbase": "...00ffffffff10028122000b2f454233322f414431322ffff..."

  # Block version (integer):
  "version": 536870912,
  
  # Difficulty (hex string):
  "nBits": "207fffff",
  
  # Block time (integer):
  "time": 1528925409,
  
  # Merkle branch for the block, proving that this coinbase is part of the block (list of hex strings):
  "merkleProof": [
   "ff12771afd8b7c5f11b499897c27454a869a01c2863567e0fc92308f01fd2552",
   "d7fa501d5bc94d9ae9fdab9984fd955c08fedbfe02637ac2384844eb52688f45"
  ]
 }
```


#### RPC submitminingsolution

##### Arguments:
```
{
  # ID from getminingcandidate RPC (integer):
  "id": 14,

  # Miner generated nonce (integer):
  "nonce": 1804358173,

  # Modified Coinbase transaction (hex string encoded binary transaction, optional): 
  "coinbase": "...00ffffffff10028122000b2fc7237b322f414431322ffff...",
  
  # Block time (integer, optional):
  "time": 1528925410,
  
  # Block version (integer, optional):
  "version": 536870912
}
```

##### Returns:

Exactly the same as ***submitblock***.  None means successful, error string or JSONRPCException if there is a problem.


## BIP135-based feature voting

BIP135 is an enhancement of BIP9, allowing miners to vote for features by setting certain bits in the version field of solved blocks.
The definition of the meaning of the bits in the version field changes and is found in config/forks.csv.  You may define your own bits, however
such a definition is not valuable unless the vast majority of miners agree to honor those bit definitions.
Detailed information is available in doc/bip135-genvoting.md.

Miners may enable voting for certain features via the "mining.vote" configuration parameter.  Provide a comma separate list of feature names.
For example, if forks.csv defines three features "f0", "f1" and "f2", you might vote for "f1" and "f2" via the following configuration setting:

```
mining.vote=f0,f1
```

This parameter can be accessed or changed at any time via the "get" and "set" RPC calls.

## Setting your excessive block size and accept depth

Blocks larger than the excessive block size will be ignored until "accept depth" (see next section) blocks are  built upon them.  This allows miners to discourage blocks that they feel are excessively large, but to ultimately accept them if it looks like the majority of the network is accepting this size.  You can learn more about these parameters [here](https://medium.com/@peter_r/the-excessive-block-gate-how-a-bitcoin-unlimited-node-deals-with-large-blocks-22a4a5c322d4#.bhkz538kw), and a miner's opinion on how they should be set [here](https://medium.com/@ViaBTC/miner-guide-how-to-safely-hard-fork-to-bitcoin-unlimited-8ac1570dc1a8#.zdklfb67p).

To change the largest block that Bitcoin Unlimited will generate, run:
```sh
bitcoin-cli setexcessiveblock blockSize acceptDepth
```
For example, to set 1MB blocks with an accept depth of 10 blocks use:
```sh
bitcoin-cli setexcessiveblock 1000000 10
```


To change the excessive block size field in bitcoin.conf or on the command line, set the excessiveblocksize config variable to a value in bytes:
 > `excessiveblocksize=<NNN>`
 
for example, to set 3MB blocks use:
 > excessiveblocksize=3000000

To change the accept depth field in bitcoin.conf or on the command line, set the excessiveacceptdepth config variable to a value (in blocks):
 > `excessiveacceptdepth=<NNN>`
 
for example, to wait for 10 blocks before accepting an excessive block, use:
 > excessiveacceptdepth=10


To discover these settings in a running bitcoind, use "getexcessiveblock".  For example:
```sh
$ bitcoin-cli getexcessiveblock
{
  "excessiveBlockSize": 16000000,
  "excessiveAcceptDepth": 4
}

```


## Setting your subversion string (spoofing the user agent)

To hide that this is a Bitcoin Unlimited node, set the "net.subversionOverride" to a string of your choice, in the bitcoin.conf file or using ./bitcoin-cli:

```sh
 bitcoin-cli set net.subversionOverride="Your Choice Here"
```

To show the current string:

```sh
bitcoin-cli get net.subversionOverride
```

To change this field in bitcoin.conf or on the command line, use:
 > net.subversionOverride=<YourChoiceHere>


## Setting your maximum mined block

By default, the maximum block that Bitcoin Unlimited will mine is 1MB (compatible with Bitcoin Core).
You may want to increase this block size if the bitcoin network as a whole is willing to mine larger blocks, or you may want to decrease this size if you feel that the demands on the network is exceeding capacity.

To change the largest block that Bitcoin Unlimited will generate, run:
```sh
bitcoin-cli setminingmaxblock blocksize
```
For example, to set 2MB blocks, use:
```sh
bitcoin-cli setminingmaxblock 2000000
```
To change this field in bitcoin.conf or on the command line, use:
 > `blockmaxsize=<NNN>`
 
for example, to set 3MB blocks use:
 > blockmaxsize=3000000

You can discover the maximum block size by running:
```sh
bitcoin-cli getminingmaxblock
```
 - WARNING: Setting this max block size parameter means that Bitcoin may mine blocks of that size on the NEXT block.  It is expected that any voting or "grace period" (like BIP109 specified) has already occurred.
 

## Setting your block version

Miners can set the block version flag via CLI/RPC or config file:

From the CLI/RPC, 
```sh
bitcoin-cli setblockversion (version number or string)
```
For example:

The following all choose to vote for 2MB blocks:
```sh
bitcoin-cli setblockversion 0x30000000
bitcoin-cli setblockversion 805306368
bitcoin-cli setblockversion BIP109
```

The following does not vote for 2MB blocks:
```sh
bitcoin-cli setblockversion 0x20000000
bitcoin-cli setblockversion 536870912
bitcoin-cli setblockversion BASE
```

You can discover the current block version using:
```sh
bitcoin-cli getblockversion
```
From bitcoin.conf:
 > blockversion=805306368

Note you must specify the version in decimal format in the bitcoin.conf file.
Here is an easy conversion in Linux: python -c "print '%d' % 0x30000000"

 - WARNING: If you use nonsense numbers when calling setblockversion, you'll end up generating blocks with nonsense versions!

## Setting your block retry intervals

Bitcoin Unlimited tracks multiple sources for data an can rapidly request blocks or transactions from other sources if one source does not deliver the requested data.
To change the retry rate, set it in microseconds in your bitcoin.conf:

Transaction retry interval:
 > txretryinterval=2000000
 
 Block retry interval:
 > blkretryinterval=2000000

## Setting your memory pool size

A larger transaction memory pool allows your node to receive expedited blocks successfully (it increases the chance that you will have a transaction referenced in the expedited block) and to pick from a larger set of available transactions.  To change the memory pool size, configure it in bitcoin.conf:

 > `maxmempool=<megabytes>`

So a 4GB mempool would be configured like:
 > maxmempool=4096

## Setting your Coinbase string

To change the string that appears in the coinbase message of a mined block, run:

```sh
bitcoin-cli setminercomment "your mining comment"
```

To show the current string:

```sh
bitcoin-cli getminercomment
```

 - WARNING: some mining software and pools also add to the coinbase string and do not validate the total string length (it must be < 100 bytes).  This can cause the mining pool to generate invalid blocks.  Please ensure that your mining pool software validates total string length, or keep the string you add to Bitcoin Unlimited short.


## Filling a new node's transaction memory pool

When you restart bitcoind, the memory pool starts empty.  If a block is found quickly, this could result in a block with few transactions.  It is possible to "prime" a new instance of bitcoind with the memory pool of a different Bitcoin Unlimited node.  To do so, go to the CLI on the node that has a full mempool, connect to your new node, and push the transactions to it.

```sh
bitcoin-cli addnode <new node's IP:port> onetry
bitcoin-cli pushtx <new node's IP:port>
```

## Validating unsolved blocks

Bitcoin Unlimited can be used to validate block templates received from other Bitcoin Unlimited releases or other bitcoin clients.  This ensures that Bitcoin Unlimited will accept the block once it is mined, allowing miners to deploy multiple clients in their mining networks.  Note that this API will return an error if the block is not built off of the chain tip seen by this client.  So it is important that the client be fully synchronized with the client that creates the block template.  You can do this by explicitly connecting them via "addnode".

The block validation RPC uses the same call syntax as the "submitblock" RPC, and returns a JSONRPCException if the block validation fails.  See "qa/rpc-tests/validateblocktemplate.py" for detailed python examples.

```sh
bitcoin-cli validateblocktemplate <hex encoded block>
```

