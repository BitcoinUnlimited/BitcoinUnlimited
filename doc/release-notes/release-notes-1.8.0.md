Release Notes for BCH Unlimited 1.8.0
======================================================

BCH Unlimited version 1.8.0 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://github.com/BitcoinUnlimited/BitcoinUnlimited/issues>

This is a major release of BCH Unlimited compatible with the upcoming protocol upgrade of the Bitcoin Cash network. You could find
May 15th, 2020 upgrade specifications here:

- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2020-05-15-upgrade.md

The following is a list of the previous network upgrades specifications:

- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/uahf-technical-spec.md (Aug 1st '17, ver 1.1.0.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/nov-13-hardfork-spec.md (Nov 13th '17, ver 1.1.2.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/may-2018-hardfork.md (May 15th '18, ver 1.3.0.0, 1.3.0.1, 1.4.0.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2018-nov-upgrade.md (Nov 15th '18, ver 1.5.0.0, 1.5.0.1, 1.5.0.2, 1.5.1.0)
- https://gitlab.com/bitcoin-cash-node/bchn-sw/bitcoincash-upgrade-specifications/-/blob/master/spec/2019-05-15-upgrade.md (May 15th '19, ver 1.6.0.0)

Upgrading
---------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Bitcoin-Qt (on Mac) or
bitcoind/bitcoin-qt (on Linux).

Main Changes in 1.8.0
---------------------

This is list of the main changes that have been merged in this release:

- Project rebrand to BCH Unlimited
- Increase length of unconfirmed transaction to 500
- Drastically improve performance of mempool management
- OP_REVERSEBYTES implementation
- SigChecks implementation
- Failure recovery for Graphene
- Graphene improve block construction reliability
- Improve QA tests (both units and functional tests)
- Reduce resource requirement for Parallel Validation
- Fix datadir compatibility problem due to unspecified ABC parking/unparking chain concept
- Clean up and update the seeders list
- Various improvements to deadlock detectors
- Misc improvements to IBD
- Use ctor to improve fetching tx from disc (txindex=1)
- Add a priority queue for urgent message processing
- Documentation update and improvements
- Improve ElectrsCash integration
- Rewrite of the fee estimator


Features Details
----------------

### Increase length of unconfirmed transaction to 500 and mempool management improvements

In previous release (1.7) we [improved](https://github.com/BitcoinUnlimited/BitcoinUnlimited/blob/release/doc/release-notes/release-notes-bucash1.7.0.md#new-cpfp-and-long-chains-of-unconfirmed-transactions) the code base to deal with chain of unconfirmed transactions in the mempool during the `getblocktemplate` work by 2 order of magnitudes. In this release O(n^2) complexity was [removed]( https://github.com/BitcoinUnlimited/BitcoinUnlimited/pull/2020) from the code that evict transactions from the mempool after they are included in a block that won the PoW lottery, and the [code]( https://github.com/BitcoinUnlimited/BitcoinUnlimited/pull/2032) that adds a transaction to a long chain in the mempool was optimized.

That let us increase the maximum length on unconfirmed transactions accepted in the mempool to 500 without hitting any penalty performance wise.

Increasing unilaterally such parameter is not something a single implementation, especially if not the most used, could do without some care. The problem is that this parameter is something we call "quasi-consensus" or in other words: there exists network wide configuration values that, if inconsistent, cause undesirable network behavior (but do not cause a fork).

An example of this undesirable side effect is increasing the chance of success of double spend attacks. This is due to the fact that if BCH Unlimited accept, let's say, chain as long as 500 transactions whereas all the rest of the network set the limit to 50. To mitigate this side effect we implemented Intelligent unconfirmed transaction forwarding, i.e. when a block comes in that confirms enough parent transactions to make the transaction valid in non-BU mempools, a double-spender is essentially racing the entire BU network to push his double-spend into the miner nodes that now accept the transaction.

Another unwanted side effect is the opening of new attack vector, which is usually called [reverse respend attack](https://gist.github.com/sickpig/231fbde2839a889d67848180d65f17b0#attack-scenario-2-reverse-respend). Basically the attacker leverage the discrepancy of network policy mixing unconfirmed UTXO with confirmed UTXO trying to be successful at double spending. This attack is completely solved by BCH Unlimited adding a constraint on transactions that has more than ABC max ancestor limits parents, which is that those could spend only one input, hence the attacker cannot mix UTXOs, so no reverse respend attack can't be carried out.

### Failure recovery for Graphene

Graphene is a probabilistic set reconciliation protocol that trades smaller block sizes for an increase in failure rate. Currently, Graphene is tuned so that one block is expected to fail each day, but far fewer failures are observed in practice. Nevertheless, failures do occur and the remedy is to restart the block download process using an alternative protocol such as XThin or Compact Blocks. In this release we implemented the failure recovery scheme introduced in the [most recent](https://people.cs.umass.edu/~gbiss/graphene.sigcomm.pdf) Graphene paper in order to salvage the block instead of starting over.

The failure recovery protocol uses a second Bloom filter and IBLT. One of the most important aspects to achieving optimal failure recovery size and performance is in properly setting the parameters for these data structures as detailed in the paper. I have made a [separate effort](https://github.com/bissias/graphene-experiments/blob/master/jupyter/graphene_v2_param_estimates.ipynb) to the translate the equations from the paper into python code, which I then compared against the paper's numerical results for several representative examples. For this PR, I translated the python code to C++ and also implemented a unit test to ensure that output for one example test point matches that of the python code.

Commit details
--------------

- `594bde7e8` fix doublespend issue in schnorrsig test (#2160) (Andrew Stone)
- `bdf535868` unnecessary to convert to and from UniValue (#2159) (Andrew Stone)
- `5ab729661` On startup, use reconsidermostworkchain() when chain not synced (#2158) (Peter Tschipper)
- `f8e612aa1` implement sigchecks consensus rules (Andrew Stone)
- `ea08074ca` cherry pick from ABC 18708a54d [sigcheck] Add per tx limit (Amaury Séchet)
- `3676158e3` cherry pick ABC 276a95b871 (Andrew Stone)
- `9a804694c` [standardness] activate SCRIPT_VERIFY_INPUT_SIGCHECKS in next upgrade (Mark Lundeberg)
- `4cd9393cc` implement sigcheck limits as defined in the 2020 may hard fork. (Andrew Stone)
- `d050e39a3` implement sigcheck counting as defined in the 2020 may hard fork at the script level.  Enforcement of sigcheck limits is not implemented in this commit. (Andrew Stone)
- `0fdb12127` Thread locking (#2157) (Andrew Stone)
- `64765352f` connections are not necessarily evicted in connection order.  update nodehandling.py test to reflect that (#2156) (Andrew Stone)
- `8c313e37b` fix comptool problem (#2154) (Andrew Stone)
- `1f65d1122` increase port range to minimize conflicts when running rpc_tests.py (#2155) (Andrew Stone)
- `d15f1710a` Gradually decay assigned misbehavior over time (#2143) (Peter Tschipper)
- `39d4a4405` Use MAX_THINTYPE_BLOCKS_IN_FLIGHT to determine the chain length… (#2152) (Peter Tschipper)
- `82effdedb` Refactor ReconstructBlock in graphene (#2147) (bissias)
- `31e3c4166` Add missing lock to CDB Flush (#2153) (Peter Tschipper)
- `cf25f0f18` [qa] Add cashaccount activation height argument (#2151) (dagurval)
- `f20dc55b9` [qa] Improved version parsing in test (#2150) (dagurval)
- `5af869aa9` [layout] Update various icon and splash screen using the new na… (#2148) (Andrea Suisani)
- `cac784264` [qa] Add a timeout parameter to sync_blocks (#2138) (Andrea Suisani)
- `99273531d` Use a CNodeRef rather than explicit reference  counting in PV (#2145) (Peter Tschipper)
- `4ce5b9e96` [rebrand] BUcash to BCH Unlimited (#2142) (Andrea Suisani)
- `6503ffc1c` fix lockheld checks, throw meaningful logic errors rather than… (#2140) (Griffith)
- `1b5465279` delete duplicate bunode.py file (#2144) (Griffith)
- `dfb4fb1c7` Fix mempool_packages.py with long chained transactions (#2139) (Peter Tschipper)
- `e434cb828` Fix a pack of nits (#2137) (Peter Tschipper)
- `1ef846738` Add missing locks and fix false positive clang warnings - part 3 (#2126) (Andrea Suisani)
- `32e87d60b` some graphene block fixes (#2130) (Andrew Stone)
- `3331b775a` Make IsAlreadyValidating() more granular by checking for hash (#2133) (Peter Tschipper)
- `76d3ab410` op_reversebytes_activitation fix (#2131) (Andrew Stone)
- `c6b4c4942` Updating bitcoinforks.org operetade seeders. (#2123) (Andrea Suisani)
- `e9ad1a50a` Turn variables that store CB/GR salt used for any given peers i… (#2120) (Andrea Suisani)
- `956d4b8e4` check the correct vBlocksToAnnounce (#2125) (Griffith)
- `383b9729a` randomlyDontInv tweak has to affect only to MSG_TX INVs (#2127) (Andrea Suisani)
- `d96fe626a` Add missing locks and fix false positive clang warnings - part 2 (#2124) (Andrea Suisani)
- `cd7763c79` improve lock order debug printing (#2128) (Griffith)
- `3d6201103` enforce lock ordering by checking pointer values instead of mutex name (#2116) (Griffith)
- `0819d3aba` Add missing locks - part 1 (#2121) (Andrea Suisani)
- `4a41ee6bc` Just use 3 nodes in mempool_push.py (Andrea Suisani)
- `f7ae44e67` Use new getters to retrieve BCH default policy for chain of unconfirmed txs (Andrea Suisani)
- `93cc85090` Add new post-fork ancestor and descendants limit. (Dagur Valberg Johannsson)
- `536f44a0c` Move mempool policy constants to policy/mempool.h (Dagur Valberg Johannsson)
- `092601dc2` Silence sync.h warning we got when  compiling with clang `-Wthread-safety-analysis` on (Andrea Suisani)
- `7457db829` fix lock ordering issue between cs_utxo and cs_blockvalidationthread (Greg-Griffith)
- `8318646dc` Add missing cs_vSend while accessing vSendMsg.size() (Andrea Suisani)
- `910b753ce` Annotate GetTotalRecvSize with EXCLUSIVE_LOCKS_REQUIRED(cs_vRecvMsg) (Andrea Suisani)
- `2fdac73bf` Use correct critical section name in net.h GUARDED_BY macro (Andrea Suisani)
- `65ff1b023` Silence sync.h warning we got when  compiling with clang `-Wthread-safety-analysis` on (Andrea Suisani)
- `a660fde2b` Set default txn chain length to 500 (#2080) (Peter Tschipper)
- `960bae26a` Formatting prevector code and related unit tests/benchs (Andrea Suisani)
- `328f446d6` prevector: avoid misaligned member accesses (Anthony Towns)
- `7d99c72a5` Use correct C++11 header for std::swap() (Hennadii Stepanov)
- `af4eb785c` Remove unused includes (practicalswift)
- `257bf7988` speed up Unserialize_impl for prevector (Akio Nakamura)
- `ff7c1bb0f` Drop defunct prevector compat handling (Ben Woosley)
- `c8582b431` warnings: Compiler warning on memset usage for non-trivial type (Lenny Maiorani)
- `e3a0a5a6e` refactor: Lift prevector default vals to the member declaration (Ben Woosley)
- `47485b11a` Explicitly initialize prevector _union (Ben Woosley)
- `dcbe3e5f2` Remove default argument to prevector constructor to remove ambiguity (Ben Woosley)
- `0b52ede4c` Explicitly initialize prevector::_union to avoid new warning (Matt Corallo)
- `39687de10` Make prevector::resize() and other prevector operations much faster (Wladimir J. van der Laan)
- `bee59af43` prevector: assert successful allocation (Cory Fields)
- `dd1298abf` Fix header guards using reserved identifiers (Dan Raviv)
- `f4fe81ebd` Port of Core  #9505: Prevector Quick Destruct (Jeremy Rubin)
- `db507eedb` Add SSE41 and AVX2 libreary to bitcoin-miner linker flag (Andrea Suisani)
- `28d530e73` Fix --disable-asm for newer assembly checks/code (Luke Dashjr)
- `927e87b20` build: always attempt to enable targeted sse42 cxxflags (Cory Fields)
- `20507a629` Enable double-SHA256-for-64-byte code on 32-bit x86 (Pieter Wuille)
- `856aafb1a` 8-way AVX2 implementation for double SHA256 on 64-byte inputs (Pieter Wuille)
- `5a35a870f` 4-way SSE4.1 implementation for double SHA256 on 64-byte inputs (Pieter Wuille)
- `3902527ad` Use SHA256D64 in Merkle root computation (Pieter Wuille)
- `8f8a13dbf` Specialized double sha256 for 64 byte inputs (Pieter Wuille)
- `80696d3c3` Refactor SHA256 code (Pieter Wuille)
- `1465cea00` Benchmark Merkle root computation (Pieter Wuille)
- `1f5376729` OP_REVERSEBYTES activation logic (tobiasruck)
- `79c912c7c` Add create_tx_with_script to blocktools.py (Andrea Suisani)
- `ad1b22cb1` Move pad_tx from txtools.py to blocktools.py (Andrea Suisani)
- `17bb4db70` Added OP_REVERSEBYTES+implementation, added (always disabled) activation flag (tobiasruck)
- `a00cfab1c` Fix a conflict due to the merge of #2068 (Andrea Suisani)
- `eefbbf11f` Simplify testing RNG code (Pieter Wuille)
- `2e5c507a3` Make unit tests use the insecure_rand_ctx exclusively (Pieter Wuille)
- `4be4f5f52` Bugfix: randbytes should seed when needed (non reachable issue) (Pieter Wuille)
- `222a18afe` Introduce a Shuffle for FastRandomContext and use it in wallet and coinselection (Pieter Wuille)
- `c74b9b2e2` Use a local FastRandomContext in a few more places in net (Pieter Wuille)
- `63fcdb48a` Make addrman use its local RNG exclusively (Pieter Wuille)
- `58be83a48` Fix FreeBSD build by including utilstrencodings.h (Wladimir J. van der Laan)
- `e4fb4e0b7` Make FastRandomContext support standard C++11 RNG interface (Pieter Wuille)
- `0f4e31059` Minimal code changes to allow msvc compilation. (Aaron Clauson)
- `a5f86d49b` Use nullptr instead of the macro NULL in random.cpp (Andrea Suisani)
- `d73104ec9` Check if sys/random.h is required for getentropy on OSX. (James Hilliard)
- `55f72968b` random: only use getentropy on openbsd (Cory Fields)
- `2961e8d48` Clarify entropy source (Pieter Wuille)
- `0742c908e` Use cpuid intrinsics instead of asm code (Pieter Wuille)
- `99832ee4b` random: fix crash on some 64bit platforms (Cory Fields)
- `7d679eb11` Add RandAddSeedSleep (Matt Corallo)
- `789c5076e` Add internal method to add new random data to our internal RNG state (Matt Corallo)
- `993c646e7` Maintain state across GetStrongRandBytes calls (Pieter Wuille)
- `04b8c30f2` Use sanity check timestamps as entropy (Pieter Wuille)
- `bc265ff00` Test that GetPerformanceCounter() increments (Pieter Wuille)
- `9260157f2` Use hardware timestamps in RNG seeding (Pieter Wuille)
- `aaea4badc` Add attribute [[noreturn]] (C++11) to functions that will not return (practicalswift)
- `6c74b4489` Fix resource leak (Dag Robole)
- `eee0f220a` Use rdrand as entropy source on supported platforms (Pieter Wuille)
- `1338d1cad` scripted-diff: Use new naming style for insecure_rand* functions (Pieter Wuille)
- `0947c5117` scripted-diff: Use randbits/bool instead of randrange where possible (Pieter Wuille)
- `be6dd066a` Use randbits instead of ad-hoc emulation in prevector tests (Pieter Wuille)
- `574f88313` Replace rand() & ((1 << N) - 1) with randbits(N) (Pieter Wuille)
- `b4e4e868d` Make CScript (and prevector) c++11 movable. (Pieter Wuille)
- `4ec35923c` Replace more rand() % NUM by randranges (Pieter Wuille)
- `fe1ddf539` scripted-diff: use insecure_rand256/randrange more (Pieter Wuille)
- `285dedab3` Add various insecure_rand wrappers for tests (Pieter Wuille)
- `feec3a9ee` Merge test_random.h into test_bitcoin.h (Pieter Wuille)
- `af4f97bfd` Deduplicate SignatureCacheHasher (Jeremy Rubin)
- `233ad91f2` Add FastRandomContext::rand256() and ::randbytes() (Pieter Wuille)
- `c7aca8bcc` Add a FastRandomContext::randrange and use it (Pieter Wuille)
- `067932805` Switch FastRandomContext to ChaCha20 (Pieter Wuille)
- `3d5ca18eb` Add ChaCha20 (Wladimir J. van der Laan)
- `8684152d7` Introduce FastRandomContext::randbool() (Pieter Wuille)
- `3804487bf` random: Add fallback if getrandom syscall not available (Wladimir J. van der Laan)
- `4a8d6f2e9` sanity: Move OS random to sanity check function (Wladimir J. van der Laan)
- `93891a7cd` util: Specific GetOSRandom for Linux/FreeBSD/OpenBSD (Wladimir J. van der Laan)
- `4c79f9c80` Don't use assert for catching randomness failures (Pieter Wuille)
- `65d6e1eb7` Always require OS randomness when generating secret keys (Pieter Wuille)
- `ff4257007` Build cashlib only on linux (#2115) (Andrea Suisani)
- `d6ba937f8` update all seenLockOrder maps when a new lock is seen (#2110) (Griffith)
- `91e517e8d` No need to explicitly add SCRIPT_ENABLE_CHECKDATASIG (Andrea Suisani)
- `9017b387b` Fix a comment minimaldata.py python test (Andrea Suisani)
- `857709dbd` Remove SCRIPT_VERIFY_MINIMALDATA from the set of standard flags (Andrea Suisani)
- `809ae618c` add SCHNORR_MULTISIG to mandatory flags (Mark Lundeberg)
- `b0a361c35` add SCRIPT_VERIFY_MINIMALDATA to mandatory flags (Mark Lundeberg)
- `55c9080a3` Reformat MANDATORY_SCRIPT_VERIFY_FLAGS and add clang-format exception (Andrea Suisani)
- `ede381402` drop 'check3' upgrade-conditional-script-failure for Schnorr multisig (Mark Lundeberg)
- `68c606212` Sync cashlib python test data structure with C++ ones (Andrea Suisani)
- `4657a0ea6` Make more script validation flags backward compatible (Mark Lundeberg)
- `a90e9bfc5` Remove NULLDUMMY (Mark Lundeberg)
- `34aedc8a2` [travis] remove BU special casing to avoid exiting before the timeout (Andrea Suisani)
- `383e66837` [Minor enhancement]  Startup txindex after ActivateBestChain (#2094) (Andrew Stone)
- `8d2a872ee` Remove handling for xthin versions < 2 (Peter Tschipper)
- `3ec2a3195` Update the name tweak used to set/change network upgrade time (Andrea Suisani)
- `908e53caf` [travis] Give more time to run unit tests (#2108) (Andrea Suisani)
- `54e7b210e` Add opcodes_tests.cpp to .formatted_files (#2106) (Andrea Suisani)
- `1ffdf7bb2` clean up some deadlock detector code (#2103) (Griffith)
- `956270cdb` replace multimaps with safer datastructures (#2084) (Griffith)
- `2cd680d67` Bump version to 1.8 (#2101) (Andrea Suisani)
- `9166b7513` Fix edge case in request manager (#2102) (Peter Tschipper)
- `c8d847819` A few IBD tweaks (#2050) (Peter Tschipper)
- `c9da8e332` Remove bitprim seeders (#2099) (Andrea Suisani)
- `f78ac9e52` Run excessive.py again in our QA test suites (#2091) (Andrea Suisani)
- `859d73737` dont fetch coin from view 3 times when one will do. (#2093) (Griffith)
- `64287c668` Up the reserved diskspace in CheckDiskSpace() to 100MB (#2095) (Peter Tschipper)
- `03ee02fe1` Remove EC for sigops (#2097) (Peter Tschipper)
- `a0d44f971` remove unused state param from updatecoins and spendcoins (#2092) (Griffith)
- `57f5d3256` [travis] Remove constraints on build time for dependencies and… (#2096) (Andrea Suisani)
- `05298181a` Change Nov 2019 net upgrade activation to be height based (#2098) (Andrea Suisani)
- `531027bcf` Better performance for checking p2sh sigops (#2089) (Peter Tschipper)
- `323a0de80` Mention you need `--allow-modified` to build ElectrsCash from a… (#2085) (Andrea Suisani)
- `87c8f91e9` Update univalue subtree (#2086) (Andrea Suisani)
- `9ef026890` Add graphene recovery messages to the priority queue (#2087) (Peter Tschipper)
- `4c14dd8f7` Implement a release mode action for DbgAssert in `getchaintxsta… (#2090) (Andrea Suisani)
- `6403cd77c` [port][rpc] Add getchaintxstats and uptime RPC calls (#2070) (Andrea Suisani)
- `0ace937dc` Increase the second timeout slightly (from 2K to 2.1K) (Andrea Suisani)
- `e9357ecf5` Reduce time threshold to force travis to save cache and abort (Andrea Suisani)
- `03711bafb` Update travis.yml according to travis backend errors and warns (Andrea Suisani)
- `06ab9708e` Failure recovery for Graphene (#2030) (bissias)
- `a70bb86ae` Do not reset the pblock smart pointer when clearing data (#2083) (Peter Tschipper)
- `66e5d69c5` fix issue allowing the creation of duplicate reconstruct templa… (#2082) (Griffith)
- `b3367b729` Don't add more than one unique thintype block in flight (#2081) (Peter Tschipper)
- `3471f8bf1` Update to Graphene spec for changes in v2.2 (#2055) (bissias)
- `16df85df7` Speed up the loading of the block index (#2078) (Peter Tschipper)
- `1a7454a6b` [qa] Add -electrum-only filter (Dagur Valberg Johannsson)
- `05f46d973` [qa] Add test for server.features (Dagur Valberg Johannsson)
- `74556246b` [electrum] Tests for 'blockchain.transaction.get' (Dagur Valberg Johannsson)
- `526480bec` [electrum] More debug info in `getelectruminfo` (Dagur Valberg Johannsson)
- `5f2f59402` [qa] Test for tx chain with same scripthash (Dagur Valberg Johannsson)
- `b95feae7f` [qa] Test for electrum.blockchain.get_history (Dagur Valberg Johannsson)
- `9a0d738df` [qa] Allow passing prev as hexstr in create_block (Dagur Valberg Johannsson)
- `65f54d8e5` [qa] Electrum helper functions (Dagur Valberg Johannsson)
- `cc264d5af` Add a test for node eviction (Peter Tschipper)
- `752676174` fix ordering in compare_exchange_weak (Peter Tschipper)
- `257428c58` [electrum] Add test for unknown method (#2071) (dagurval)
- `7083533e5` GetTransaction improvements (#2068) (dagurval)
- `47a4195bb` Cleanup inactivity checking (Peter Tschipper)
- `3e1825327` Add the rest of the GUARDED_BY statements in CNode (Peter Tschipper)
- `1a9e16bb2` Add a few more atomic vars in CNode (Peter Tschipper)
- `d6ae22005` In CNode make nActivityBytes atomic (Peter Tschipper)
- `7eedf9b2d` [Giga-Net:] Add a priority queue for urgent message processing (#1721) (Peter Tschipper)
- `07cd800da` Add Nov 15th 2019 checkpoints for mainnet and testnet (#2053) (Andrea Suisani)
- `db9cfdf6c` Build latest electrscash (#2069) (Andrew Stone)
- `d3f106aa2` Get the subver string earlier when we track inbound connections (#2063) (Peter Tschipper)
- `603763ade` Use BU BCH ppa repository as Berkely DB packages provider (#2065) (Andrea Suisani)
- `828bd1c93` [gui] Fix input validation for traffic shaping parameters (#2061) (Andrea Suisani)
- `a9c8f9234` Update copyright year to 2020 (#2058) (Marius Kjærstad)
- `e77ae4aea` Update ElectrsCash build instruction to take into account binary rename (#2062) (Andrea Suisani)
- `43d4c7d44` Do not ban a peer for sending a null hash with inv - just ignore instead (#2060) (Peter Tschipper)
- `191462975` Fix a warning in cashlib.cpp (#2054) (Andrea Suisani)
- `a763bc65a` Implement two more ban reason codes (#2052) (Peter Tschipper)
- `441ba0b5c` Don't run CheckBlock() more than once during IBD (#2057) (Peter Tschipper)
- `a777c7412` fix cashlib decodebase64 error return (#2056) (Andrew Stone)
- `21aaee43b` Turn off PV for compactblocks_2.py (#2051) (Peter Tschipper)
- `87c065d9c` [Requires #2020] Don't make copies of CTxMemPoolEntry's in RemoveForBlock() (#2042) (Peter Tschipper)
- `0e4b5e6f1` Improve block post processing from O(n^2) to O(n) (#2020) (Peter Tschipper)
- `22d2124bc` Reduce txadmission ancestor chain parsing  by roughly 50% for long ch… (#2032) (Peter Tschipper)
- `c040c70f9` Clarify comments in CalculateMemPoolAncestors when looking up p… (#2043) (Peter Tschipper)
- `7bddccaf1` Reduce the retry inteval during IBD (#2049) (Peter Tschipper)
- `0284d3d07` Improve performance of blocksdb during IBD (#2044) (Peter Tschipper)
- `9a13c5588` Make the ban reason visible in the QT Banned Peers table (#2048) (Peter Tschipper)
- `5b1b7fd3e` don't send inv for mempool sync objects (#2046) (bissias)
- `b8be44170` Start pruning slow peers before we get to the max outbound connections (#2047) (Peter Tschipper)
- `c453bd169` Flush the blockindex at regular intervals during IBD (#2045) (Peter Tschipper)
- `2a4893d6a` do not use smart pointer references (#2039) (Peter Tschipper)
- `f81673204` Do not std::move(ptx) when passing a parameter (#2040) (Peter Tschipper)
- `2c979f466` enhance the hd44 derivation code to handle 64 byte master seeds (#2041) (Andrew Stone)
- `fdfcd42a6` Improve MatchFreezeCLTV() according to gandrewstone feedback (Andrea Suisani)
- `532448906` Fix a typo in MatchFreezeCLTV() (Andrea Suisani)
- `2cdd07014` Introduce PUBLIC_KEY_HASH160_SIZE and use it in standard.cpp (Andrea Suisani)
- `b3e587dce` Remove template matching and pseudo opcodes (Pieter Wuille)
- `18d96c4b3` Minor nits in Xversion doc and comments (#2038) (Andrea Suisani)
- `d93350541` [Electrum] Flag electrum server in xversion  (#2037) (dagurval)
- `4ff005ad4` Port wallet optimizations from old coinselection branch. (Andrew Stone)
- `aa24d86bc` Improve schorrsig.py reliability (Peter Tschipper)
- `4a6083709` Improve reliability of compactblocks_2.py (Peter Tschipper)
- `bd1899f38` Do not process any more blocks if shutdown is requested (Peter Tschipper)
- `3b6ec3384` Add ./autogen.sh and mention installing curl. (Søren Bredlund Caspersen)
- `a1fb38d07` scanelf needs the right binary file (Søren Bredlund Caspersen)
- `92c2bd9f8` Remove boost build instructions. (Søren Bredlund Caspersen)
- `a0cb8e9d2` add patch to BDB build instructions (Søren Bredlund Caspersen)
- `3bb4f0785` Where to find bitcoin-qt (Søren Bredlund Caspersen)
- `bcd141c07` Set the mempoolminfee to be the max of either the minrelaytxfee (#2031) (Peter Tschipper)
- `df15489c1` Add git to list, update link (Søren Bredlund Caspersen)
- `18c9ed435` net: Improve and document SOCKS code (Wladimir J. van der Laan)
- `45c0c9a16` Do not run reomoveForBlock() if we are in initial sync (#2026) (Peter Tschipper)
- `f6d0c430d` revert estimatesmartfee return obj for backwards compatibility (#2025) (Griffith)
- `411a28c14` [QA only] Isolate QA features from PR #2022 (#2027) (Andrew Stone)
- `09b83657f` add flag to enable build against libstdc++ debug mode: https://gcc.gnu.org/onlinedocs/libstdc%2B%2B/manual/debug_mode_using.html (Andrew Stone)
- `41aeb5bd9` Small changes for FreeBSD info (Søren Bredlund Caspersen)
- `628871a21` Add freebsd build guide (Søren Bredlund Caspersen)
- `6d32a59a7` Change preparations to dependencies (Søren B. Caspersen)
- `eb0dc4d6f` Cleanup unix build instructions (Søren B. Caspersen)
- `2c324a710` Make sure to lock cs_blockvalidationthread during the time (#2024) (Peter Tschipper)
- `af529ce8d` Update OpenBSD build instructions (#2021) (Søren Bredlund Caspersen)
- `a3122f1b0` move SyncStorage to before we lock mapblock index (#1764) (Griffith)
- `f8047aa95` Fee estimater fix (#899) (Griffith)
- `66af566b8` Wallet lock refactor and fixes (#2019) (Andrew Stone)
- `0134f7307` Re-enable -limitfreerealy in our unit tests (Peter Tschipper)
- `30bd0b463` [qa] Allow setting custom electrum bin path (Dagur Valberg Johannsson)
- `455f7a1c9` Erase() the blockvalidation thread id as the very last step (Peter Tschipper)
- `b371fd5b2` Trivial: Fix DbgAssert that would never fail (Justaphf)
- `9f9f6a818` Replace missing ClearCompactBlockStats() (Peter Tschipper)
- `78471be47` expose the bloom filter capacity to the java layer so that blooms can be created with a larger capacity than elements under the expectation that they will be filled later (Andrew Stone)
- `39149a188` Add missing lock to IsBlockInFlight() (Peter Tschipper)
- `97604a198` Cleanup requestmanager_tests.cpp (Peter Tschipper)
- `5792eb060` Add tests for multi thintype blocks in flight (Peter Tschipper)
- `31f50b99d` Enable multi thintype blocks in flight (Peter Tschipper)
- `0d9b29263` #1992 Fix -Wsign-compare warnings in Qt tx graph widget (Justaphf)
- `faa99af26` Avoid initial package accounting if transaction is alredy in the block (Andrea Suisani)
- `73c5e4be0` [qa] Add subscription tests for electrum server (Dagur Valberg Johannsson)
- `d11782b24` Add missing message types to allNetMessages[] (#2005) (Peter Tschipper)
- `e569de222` Add a description as to why we sort by time for ancestor transactions (#2003) (Peter Tschipper)
- `7171a7ca1` add java cashlib APIs to access existing merkle block tx extractor (Andrew Stone)
- `07a25437c` Makes getLabelPublic() to work with string longer than 16 chars (#1988) (Andrea Suisani)
- `b56e8bcc2` [gitian] Strip electrcash binary before installing (#1979) (Andrea Suisani)
- `841311512` Set the default behaviour of -limitfreerelay to zero (#2001) (Peter Tschipper)
- `a5911cf49` Fix mempool sync bug (#1994) (bissias)
- `610c16a4c` Another pass to use c++11 nullptr across the codebase when appropriate (#1995) (Andrea Suisani)
- `40c4e570d` [qa] Use connectrum in QA tests (Dagur Valberg Johannsson)
- `0f86808f8` [qa] Add connectrum electrum library (Dagur Valberg Johannsson)
- `3fde26ccc` Change the reject message from too-many-inputs to bad-txn-too-many-inputs (Peter Tschipper)
- `7f4282cc5` Fix mempool_push.py to work with restrictInputs (Peter Tschipper)
- `cd8a62a4d` Add the reject reason to some of the other tests in this suite (Peter Tschipper)
- `b003d2107` Add tweak and logic for rejecting txns that have too many inputs (Peter Tschipper)
- `116419adf` fix an undefined behavior in uint::SetHex (Kaz Wesley)
- `9c7cd812e` update APIs to pass the blockchain identifier to address encode and decode functions, allowing cashlib to easily handle regtest, testnet and mainnet addresses.  rename throwIllegalState and AddrBlockchainUnlimited for clarity (Andrew Stone)
- `f688d0c3d` Improve IBD when pruning (Peter Tschipper)
- `fa0ab2944` Add limitfreerelay to getnetworkinfo() (Peter Tschipper)
- `2adf6298a` Add readlock when accessing nSequenceId (Peter Tschipper)
- `d2ce4cc01` Remove giving DOS points for unknown inv types (Peter Tschipper)
- `157125633` Take out calls to RemoveForReorg() (Peter Tschipper)
- `9378b76d6` Add a warming about txindex database migration process to 1.7 rel notes (Andrea Suisani)
- `ef4dc8da9` Add URL for ElectrsCash changelog to the release notes for 1.7.0 (Andrea Suisani)
- `59e2eea86` Fix computation for smoothed runtime TPS statistic (Justaphf)
- `509ac8274` Force the log to flush before checking the log for entries (Peter Tschipper)

Credits
=======

Thanks to everyone who directly contributed to this release:


- Andrea Suisani
- Andrew Stone
- George Bissias
- Dagur Valberg Johannsson
- Greg-Griffith
- Justaphf
- Søren Bredlund Caspersen
- Peter Tschipper 

We have backported an amount of changes from other projects, namely Bitcoin Core, BCHN and Bitcoin ABC.

Following all the indirect contributors whose work has been imported via the above backports:

- Aaron Clauson
- Akio Nakamura
- Amaury Séchet
- Anthony Towns
- Ben Woosley
- Cory Fields
- Dag Robole
- Dan Raviv
- Hennadii Stepanov
- James Hilliard
- Jeremy Rubin
- Kaz Wesley
- Lenny Maiorani
- Luke Dashjr
- Marius Kjærstad
- Mark Lundeberg
- Matt Corallo
- Peter Tschipper
- Pieter Wuille
- practicalswift
- tobiasruck
- Wladimir J. van der Laan

