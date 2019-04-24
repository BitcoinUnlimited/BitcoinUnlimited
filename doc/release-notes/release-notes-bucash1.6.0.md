Release Notes for Bitcoin Unlimited Cash Edition 1.6.0.0
=========================================================

Bitcoin Unlimited Cash Edition  version 1.6.0.0 is now available from:

  <https://bitcoinunlimited.info/download>

Please report bugs using the issue tracker at github:

  <https://github.com/BitcoinUnlimited/BitcoinUnlimited/issues>

This is a main release candidate of Bitcoin Unlimited compatible
with the Bitcoin Cash specifications you could find here:

- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/uahf-technical-spec.md (Aug 1st '17 Protocol Upgrade, bucash 1.1.0.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/nov-13-hardfork-spec.md (Nov 13th '17 Protocol Upgrade, bucash 1.1.2.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/may-2018-hardfork.md (May 15th '18 Protocol Upgrade, bucash 1.3.0.0, 1.3.0.1, 1.4.0.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2018-nov-upgrade.md (Nov 15th '18 Protocol Upgrade, bucash 1.5.0.0, 1.5.0.1, 1.5.0.2, 1.5.1.0)
- https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2019-05-15-upgrade.md (May 15th '19 Protocol Upgrade, bucash 1.6.0.0)

This release is compatible with [Bitcoin Cash upcoming May 15th, 2019 protocol upgrade](https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2019-05-15-upgrade.md) and [SV](https://github.com/bitcoin-sv/bitcoin-sv/blob/master/doc/release-notes.md) changes to the consensus rules. SV features set is **disabled by default**, the default policy is to activate the set of changes as defined by the bitcoincash.org.

Upgrading
---------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Bitcoin-Qt (on Mac) or
bitcoind/bitcoin-qt (on Linux).

If you are upgrading from a release older than 1.1.2.0, your UTXO database will be converted
to a new format. This step could take a variable amount of time that will depend
on the performance of the hardware you are using.

Other than that upgrading from a version lower than 1.5.0.0 your client is probably stuck
on a minority chain and need some manual intervention to make so that it follow the majority
chain after the upgrade. For more detail please look at `reconsidermostworkchain` RPC commands.

Main Changes in 1.6.0
---------------------

This release will be compatible with the upcoming May, 15th 2019 BCH  protocol upgrade. It [improves](https://github.com/bissias/graphene-experiments/blob/master/jupyter/graphene_v2_interim_report.ipynb)
Graphene block propagation technique greatly and also implelemt Compact Blocks ([BIP 152](https://github.com/bitcoin/bips/blob/master/bip-0152.mediawiki)). This is list of the main changes that have
been merged in this release:

- [Segwit P2SH recovery](https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2019-05-15-segwit-recovery.md)
- [Schnorr signatures](https://github.com/bitcoincashorg/bitcoincash.org/blob/master/spec/2019-05-15-schnorr.md)
- Compact Block implementations
- Basic integration with the electrum server [electrs](https://github.com/dagurval/electrs.git) (experimental, off by default), see this [doc](../bu-electrum-integration.md) for more info
- Graphene: do not send txns oders by leveraging CTOR
- Graphene: Fast Filter instead of Bloom Filter (optional)
- Graphene: seeding of IBLT hash function
- Graphene: use cheap hashes to avoid short id tx collision
- Graphene: reduce decode failure to 0.5% via IBLT padding
- Graphene: O(1) heuristic to determine IBLT / BF size for block over 600 txns
- RPC enhancements and new commands (getrawblocktransactions, getrawtransactionssince and gettxoutproofs)
- QA reliability improvemennts
- Easier gitian build process (docker based)
- Code restructure around the 3 block propagation techs we currently support (Graphene, CB, Xthin)
- Move to C++14 and bump lib boost minimum ver to match
- Update Windows build scripts
- Simplify and update Xpedited code
- Refactor Script interpreter as a "virtual machine" encapsulated by a class. This allows the script interpreter to be used outside of bitcoind (for example, its ability to "step" allows use in a script debugger).
- Implement Xversion changeable key
- Documentation improvements

Commit details
--------------

- `135f0f995` Two more nits (#1687) (Peter Tschipper)
- `442c1bd8d` [electrum] More electrum configuration options (#1684) (dagurval)
- `3aab9b5db` a couple of little nits (#1685) (Peter Tschipper)
- `390a9e7f2` Add BUCash 1.6.0 release notes (#1671) (Andrea Suisani)
- `71834506f` Fix compact block compression data in getnetworkinfo (#1683) (Peter Tschipper)
- `065620eaa` [electum] Fix cookie dir path (#1682) (dagurval)
- `6106ed769` release thread handles when group is destructed (#1680) (Andrew Stone)
- `6d628bf06` Make sure to wait for all script validation threads to finish (#1678) (Peter Tschipper)
- `72153d295` make schnorrsig.py issue many fewer tx (#1681) (Andrew Stone)
- `228a3b553` Add electrs to gitian output (Dagur Valberg Johannsson)
- `65aa16c44` Update Rust from v1.28.0 to v1.32.0. (Taylor Hornby)
- `f95e83765` Introduce Rust to depends system. (Sean Bowe)
- `737b96eda` Build cashlib on travis so that we could run tests based on that on x… (#1662) (Andrea Suisani)
- `319ef6a40` [Builds on 1611]  Refactor compact blocks out of CNode (#1623) (Peter Tschipper)
- `16e2daf9f` use rsm_debug_assertion in configure instead of debug_assertion in util (#1676) (Griffith)
- `3ede8fd7b` add unsigned integer types to tweakbase (#1674) (Griffith)
- `5cd24e256` Basic integration with the electrum server electrs (#1633) (dagurval)
- `325df3651` Refactor Xthin data out of CNode (#1611) (Peter Tschipper)
- `a2de59cf7` add schnorr activation test, expand cashlib with datasig, fix lock on xversion (#1672) (Andrew Stone)
- `064d22117` [util] Introduce utilprocess (#1645) (dagurval)
- `b2e94fbbb` Qt: Use _putenv_s instead of setenv on Windows builds (Brian McMichael)
- `02aa05be8` introduce high and low version preferences for graphene (#1670) (bissias)
- `f72ecb26e` add get for unsigned ints from univalue class (#1665) (Griffith)
- `6bbe8aada` fix bip68-112-113-p2p.py and improve reliability (#1673) (Peter Tschipper)
- `687beecb4` add missing diff for 1589 (Greg-Griffith)
- `94cf06043` [rpc] Allow passing int for verbose in getblock (Dagur Valberg Johannsson)
- `d8ba91fc8` Fix getblocktemplate_proposals.py for the transaction size limitation (Andrew Stone)
- `1e22952c5` Fix spurious failures in bipdersig.py (Peter Tschipper)
- `9cb20a7e5` Temporarily disable maxblocksinflight.py (Peter Tschipper)
- `fc0e7f306` Fix spurious failures in mempool_packages.py (Peter Tschipper)
- `919c00bbc` Update rpm travis build and doc instructions to use boost 1.66.0 (Greg-Griffith)
- `812713111` Schnorr signature activation (Mark Lundeberg)
- `032ccca12` Add schnorr activation code based on MTP (Andrea Suisani)
- `8ce20fc00` wrap lockedtime in DEBUG_LOCKTIME define (Greg-Griffith)
- `09c11de0f` Partial port of "Add activation code for SEGWIT_RECOVERY" (D2479) (Florian)
- `3d4f40b89` Add SCRIPT_ALLOW_SEGWIT_RECOVERY (Andrea Suisani)
- `7e107aa2c` Partial port of Core's 449f9b8d (BIP141) (Andrea Suisani)
- `caea95b41` [util] throw proper exception in IsStringTrue (#1659) (dagurval)
- `b6d0a8aa3` Clear version bits warning message if conditions normalize. (#1660) (Peter Tschipper)
- `b5ec51c0a` Allow Graphene to use variant of CFastFilter (#1603) (bissias)
- `79680b995` Remove a couple of Misbehaviours (#1653) (Peter Tschipper)
- `644f2f287` Remove bip9-softforks.py (#1658) (Peter Tschipper)
- `d6a77d070` Activate November 15th 2018 upgrade by height (#1644) (Andrea Suisani)
- `b0f29c5c3` implement recursive_shared_mutex (#1591) (Griffith)
- `36250bc9e` Bump ver 1.6 (#1657) (Andrea Suisani)
- `029eca9d3` Rename USE_CLANG to NODEPENDS (#1656) (Andrea Suisani)
- `50e156f18` Fix a comparison warning in interpreter.cpp (Andrea Suisani)
- `352ca299a` add simple test for reverse_iterator (Andrew Stone)
- `33a5982e3` Rename member field according to the style guide. (Pavel Janík)
- `bce5e72b9` Introduce src/reverse_iterator.h (Jorge Timón)
- `ecbb857b3` Add script_tests.json.gen to .gitignore (Peter Tschipper)
- `54a73ccb6` Improve reliability of sync_with_ping() (Peter Tschipper)
- `ce01b796f` Improve reliablility of sendheaders.py (Peter Tschipper)
- `01975338c` Improve reliablility of p2p-acceptblock.py (Peter Tschipper)
- `c07d7e05a` add centos build to travis (#1637) (Griffith)
- `90027ee7b` implement NODE_NETWORK_LIMITED (BIP 159) (Jonas Schnelli)
- `6c98bc90f` Remove expedited log message. (#1642) (Peter Tschipper)
- `3d38cb9ca` don't run the test if cashlib is not available for this platform (Andrew Stone)
- `89a83c72a` Set a boolean to indicate whether we really have a competing validation (#1641) (Peter Tschipper)
- `67f99e028` add schnorr enable tweak and python tests using cashlib that generate a lot of schnorr signed transactions (Andrew Stone)
- `0bf4c3c99` add rpm build instructions (#1636) (Griffith)
- `427293d48` restrict multiple public labels (#1640) (Andrew Stone)
- `493941c28` Set default minlimitertxfee to 1 Sat per byte. (Andrea Suisani)
- `9ebf7e4ab` Check that a Public Label is within size constraints (#1638) (Peter Tschipper)
- `3f7df9830` add Schnorr signing module to test_framework [alternative implementation] (Mark Lundeberg)
- `456c478b0` [util] Add method http_get (Dagur Valberg Johannsson)
- `b0f94637f` fix typo and signatures because the deterministic signature algo changed (Andrew Stone)
- `7def4f5be` OpenSSL 1.1 API usage updates (gubatron)
- `5b2f277b8` resolve issue where makefile doesn't properly start lcg_tests (Andrew Stone)
- `bed85020b` Travis: Save cache when compilation took very long (MarcoFalke)
- `e5fcbd678` [build] Add missing header (Dagur Valberg Johannsson)
- `71d7945a3` [doc] Remove outdated Gitian build guide (dagurval)
- `9c62fe970` fix missing flags in script_tests and format code (Andrew Stone)
- `dc31c0cde` [qa] Support setup_clean_chain, extra_args and num_nodes (Dagur Valberg Johannsson)
- `d19d5095a` Add an algorithm identifier to the nonce generation for ECDSA signatures (Fabien)
- `3ea052b84` Update boost system m4 macros to latest upstream (Andrea Suisani)
- `e633a4aa8` [depends] Boost 1.66.0 (Andrea Suisani)
- `0f761f0fd` Enable Schnorr signature verification in CHECK(DATA)SIG(VERIFY) (Mark Lundeberg)
- `108601453` fix var contention in ram miner (Andrew Stone)
- `8c4af93c0` add SCRIPT_ENABLE_SCHNORR flag (Mark Lundeberg)
- `921ead1c2` formatting (Andrew Stone)
- `e5a8c9fd5` port schnorr sigcache changes (Mark Lundeberg)
- `704ee45dc` add include directory to get schnorr files for certain tests (Andrew Stone)
- `d4135d40a` Add CKey::SignSchnorr and CPubKey::VerifySchnorr (Mark Lundeberg)
- `81519da0c` API rename: Sign->SignECDSA, Verify->VerifyECDSA (Mark Lundeberg)
- `a58c81db3` [secp256k1] Implement Schnorr signatures (Amaury Séchet)
- `a881bdf43` delay was completing on the wrong result, causing some test failures (#1622) (Andrew Stone)
- `ac91d04be` Fix compile warning regarding out of order intitialization (#1621) (Peter Tschipper)
- `abdb4af8b` Clear graphene data first before requesting failover block (#1620) (Peter Tschipper)
- `2e3af3cbc` Add Try/Catch block to HandleBlockMessageThread() (#1616) (Peter Tschipper)
- `54b2c3d66` DRY refactor in rpc/blockchain.cpp (#1402) (Angel Leon)
- `a41b8d26b` [rpc] Allow fetching tx directly from specified block in getrawtransaction (#1612) (dagurval)
- `7ccb243f8` [rpc] Add initialblockdownload to getblockchaininfo (#1606) (dagurval)
- `c4500f301` Fix typos in net_processing and requestManager comments (#1601) (Andrea Suisani)
- `bc3dc1aa5` Command line arg for bloom targeting showing incorrect value (#1613) (Peter Tschipper)
- `5fcf2d198` Fix initializations out of order compile warning (#1614) (Peter Tschipper)
- `83b7e421a` Seed IBLT hash functions (#1582) (bissias)
- `2cdbd9dc5` Port Core PR 15549: gitian: Improve error handling (Wladimir J. van der Laan)
- `97241c1e7` ADDR message handling (#1602) (Andrew Stone)
- `9bdd7950d` Use SipHash for cheap hashes in Graphene (#1551) (bissias)
- `e69767a42` Check if datadir is valid (#1605) (dagurval)
- `2197a2ad4` Remove the check that enforce first block in UAHF (Aug 1st, 2017) is bigger than 1MB (Andrea Suisani)
- `dfc2a5449` Remove the check for IsUAHFforkActiveOnNextBlock() in tx admission process (Andrea Suisani)
- `9dac2c398` Coalesce forks helper all in the same file (Andrea Suisani)
- `cdbe1f821` Simplify and remove dead code from uahf_fork.cpp (Andrea Suisani)
- `63dc3ba16` Port core PR #10775: nCheckDepth chain height fix (romanornr)
- `43c30fbd0` add a few batch rpc calls (#1575) (Greg Griffith)
- `7877dd53c` add missing space (Søren B. Caspersen)
- `05ed9293e` More code marked as code (Søren Bredlund Caspersen)
- `2a89b1107` Mark code as code in Markdown (Søren Bredlund Caspersen)
- `8e8e73f78` markdown lists (Søren Bredlund Caspersen)
- `e8c55c354` No longer supported OS removed (Søren Bredlund Caspersen)
- `1c41a0a01` Debian/Ubuntu uses systemd now (Søren Bredlund Caspersen)
- `acaf3f32e` Add tests to check proper deserialization of CExtKey and CExtPubKey (Amaury Séchet)
- `6d39b1b76` Deserialized key should be the correct size. (John)
- `53499fd9c` add net magic override (#1445) (Andrea Suisani)
- `0676cce75` Cleanup XVal (#1588) (Peter Tschipper)
- `5eede963b` add HasData() methods to CTransaction (#1598) (Andrew Stone)
- `934bf2f83` Assert proper object type for univalue push_back and pushKV when debug enabled (#1589) (Greg Griffith)
- `4ab119d00` Reset default -g -O2 flags when enable debug (Wladimir J. van der Laan)
- `80938afb0` Headlines consistency (Søren Bredlund Caspersen)
- `6ae12447c` removed unused qt46 pathces (Andrea Suisani)
- `cd0fd1df7` Removed unused patches from qt.mk (Andrea Suisani)
- `2829447df` Run bitcoin_test-qt under minimal QPA platform (Russell Yanofsky)
- `83562a57f` Minimal version of Qt we support is 5.3.x (Andrea Suisani)
- `64bee921b` Fix a linker error while producing bitcoin-qt executable. (Andrea Suisani)
- `9aa22182f` Remove duplicate code from bitcoin_qt.m4 (Andrea Suisani)
- `70981f83a` depends: qt 5.9.7 (fanquake)
- `264da08ef` depends: Remove unused Qt 4 dependencies (Chun Kuan Lee)
- `afa0e98e6` Update Qt version in doc/dependencies.md (Andrea Suisani)
- `7225f91fe` depends: disable unused qt features (fanquake)
- `a0be303ed` depends: qt: avoid system harfbuzz and bz2 (Cory Fields)
- `34ac55a68` depends: fix bitcoin-qt back-compat with older freetype versions at runtime (Cory Fields)
- `41279fb8f` depends: fix qt determinism (Cory Fields)
- `9acc66dc9` Add aarch64 qt depends support for cross compiling bitcoin-qt (TheCharlatan)
- `dd7608e2b` Fix Qt's rcc determinism for depends/gitian (Fuzzbawls)
- `f52ec03e7` depends: use MacOS friendly sed syntax in qt.mk (Sjors Provoost)
- `2a4468ec4` Add depends 32-bit arm support for bitcoin-qt (Sebastian Kung)
- `0eee64018` Upgrade Qt depends to 5.9.6 (Sebastian Kung)
- `232074cd6` Fix depends Qt5.9.4 mac build (Ken Lee)
- `9a0c7cb9e` Ugrade Qt depends to Qt5.9.4 (Sebastian Kung)
- `9bc2ce00c` Update helper text for outscript= to reflect actual usage (#1572) (Andrea Suisani)
- `0151accaa` Build gitian using Ubuntu 18.04 based docker containers (#1594) (Andrea Suisani)
- `aabe8229a` Add table of contents to the long developer-notes file (Søren Bredlund Caspersen)
- `790cdfa57` Headers in developer-notes (Søren Bredlund Caspersen)
- `0386b6066` Add top bar back (Søren Bredlund Caspersen)
- `ebcfc8058` Upgrade to std::thread and std::chrono (#1553) (Greg Griffith)
- `f6b0c75cb` Blockstorage logic fix (#1590) (Greg Griffith)
- `6042919e7` Support larger blocks for compact blocks (Peter Tschipper)
- `1f0ba015c` Add DbgAssert in compactblocks (Peter Tschipper)
- `a49587f64` Remove the clearing of thintype data in parallel.cpp (Peter Tschipper)
- `2d7c65154` Prevent requesting get_blocktx, get_grblocktx and getblocktxn too far from tip (Peter Tschipper)
- `be2048145` Remove checks graphene, thinblock or compactblock capable (Peter Tschipper)
- `69c133e3c` Reorder getnetworkinfo order for compact stats and update help text for missing outputs (Justaphf)
- `dcb6a4110` Add compact block statistics to Debug window UI (Justaphf)
- `b0baf2a9c` A variety of nits (Peter Tschipper)
- `91bb0a4bf` remove print statement from ctor.py (Peter Tschipper)
- `448cc4760` Move thintype request tracking to the request manager. (Peter Tschipper)
- `14040c6f7` fix rebase (Peter Tschipper)
- `2f2076afc` Only send SENDCMPCT message is compact blocks is enabled. (Peter Tschipper)
- `a30838d0c` Refactor IsBlockRelayTimerEnabled method (Justaphf)
- `e8c0f543b` Use debug log level cmpctblock instead of cmpctblocks (Peter Tschipper)
- `e9bf4d956` Fix bug: ABC nodes not receiving compact blocks (Peter Tschipper)
- `b2f897724` Consolidate thin type object request tracking (Peter Tschipper)
- `a50586199` Add p2p test for BIP 152 (compact blocks) (Suhas Daftuar)
- `a11d6de96` Add support for compactblocks to mininode (Suhas Daftuar)
- `436eaa774` Remove redundant ThinType from ThinTypeRelay class (Peter Tschipper)
- `f20bbb5fd` Fix format (Peter Tschipper)
- `e8646d4ea` Fix a few tests (Peter Tschipper)
- `cc254a53c` Add get_thin message request (Peter Tschipper)
- `fdad2357d` Add request compact block as failover for graphene (Peter Tschipper)
- `201b7eaa7` Introduce BU_XTHIN_VERSION (Peter Tschipper)
- `31aa56f71` Tidy up compactblock statitics gathering (Peter Tschipper)
- `acd7fdd52` Update IsCompactBlockValid() (Peter Tschipper)
- `cef5622b6` Add some blockencodings tests (Matt Corallo)
- `42cbfdde9` Turn off compact blocks for sendheaders.py (Peter Tschipper)
- `61185eb4a` simplify code for requesting full blocks (Peter Tschipper)
- `2350acb18` request full block if too many hashes to request (Peter Tschipper)
- `346146b88` Implement Compact Blocks (BIP 152) (Peter Tschipper)
- `827f11ed4` initialize uint256 to null (Dagur Valberg Johannsson)
- `cc82ce55c` Use MakeTransactionRef in blockencodings.cpp (Peter Tschipper)
- `b36f1e4df` Implement SipHash in Python (Pieter Wuille)
- `918b7aaac` add blockencodings.cpp/.h to .formatted-files (Peter Tschipper)
- `c984215bb` Remove type and version from serialization ops (Peter Tschipper)
- `cbd1001cd` Add log level CMPCT for compact blocks (Peter Tschipper)
- `99f5d485e` Add reconstruction debug logging (Matt Corallo)
- `be653c6a4` Add partial-block block encodings API (Matt Corallo)
- `2af47d55c` Add protocol messages for short-ids blocks (Matt Corallo)
- `314e882b8` Minor text modification to thin timer. (Peter Tschipper)
- `d5834b33e` remove redundant call to ClearThinTypeBlockInFlight (George Bissias)
- `f29df7aff` clear blocks in-flight instead of block data (George Bissias)
- `45d35fb89` Avoid clearing thintype block data in parallel.cpp (#1595) (bissias)
- `30bf79622` remove unnecessary cs_main lock (Andrew Stone)
- `44dfa770a` fix deadlock and add detector to CNodeStateAccessor (Andrew Stone)
- `1d3916827` Fix unbalanced pushKV parenthesis and an instance of Pair() (Andrea Suisani)
- `5ebd36428` Add extra help text to importprivkey (Peter Tschipper)
- `b158cbc60` Add links to windows README (Søren Bredlund Caspersen)
- `35c662e64` remove checksum check since it wastes CPU for no benefit (Andrew Stone)
- `5f6766037` Update FinalizeNode() (Peter Tschipper)
- `6fceca024` remove unused, nonexistent extern decl (Andrew Stone)
- `b5ed1210f` Remove core specific action (Søren Bredlund Caspersen)
- `d34b7f6b4` Update ppa link from bitcoin-unlimited/+archive/ubuntu/bu-ppa to /~bitcoin-unlimited/+archive/ubuntu/bucash (Søren Bredlund Caspersen)
- `766d62ec8` trivial - remove number left over (Søren Bredlund Caspersen)
- `8da42455e` Headlines, lists (Søren Bredlund Caspersen)
- `99c03f8e1` Syntax, headlines (Søren Bredlund Caspersen)
- `006ad7524` Remove version number, delete link (Søren Bredlund Caspersen)
- `5c282d363` add copyright, remove irc channel (Søren Bredlund Caspersen)
- `0457fd404` syntax (Søren Bredlund Caspersen)
- `ba957ec95` Linebreak (Søren Bredlund Caspersen)
- `08d8522b7` Remove top links, minor cleanup (Søren Bredlund Caspersen)
- `b36726a5e` Links for online resources (Søren Bredlund Caspersen)
- `acad3736b` Add doc/bips-buips-specifications.md link (Søren Bredlund Caspersen)
- `6102c4c21` Links in README.md (Søren Bredlund Caspersen)
- `15f3006bd` Fix links (Søren B. Caspersen)
- `275fecfe1` Delete (now almost empty) doc/README.md (Søren B. Caspersen)
- `7f70d9282` Move from doc/README.md to /README.md (Søren B. Caspersen)
- `32efea0a0` Add links to support/discussion, remove old (core centric) links (Søren B. Caspersen)
- `cf07e85e5` Delete quick-install.md (Søren B. Caspersen)
- `68b7537cb` Remove reference to Ubuntu nightly ppa. Hasn't been updated for years. (Søren B. Caspersen)
- `4d107a3e7` Merge text about compiling from source and getting dependencies (Søren B. Caspersen)
- `efe186661` Tidy up INSTALL.md, and remove duplicate info (Søren B. Caspersen)
- `8a8d99df5` Move quick installs to INSTALL.md (Søren B. Caspersen)
- `3c1eb573b` Move setup and Initial Node Operations to INSTALL.md (Søren B. Caspersen)
- `7f76ec657` Make INSTALL.md file more informative (Søren B. Caspersen)
- `617fae3e1` Make INSTALL.md the place for install info (Søren B. Caspersen)
- `201d0ad50` Licens info in /README.md only (Søren B. Caspersen)
- `2970a2cb3` The right part is code, the rest is text (Søren Bredlund Caspersen)
- `60d06cf86` Headlines instead of broken lists (Søren Bredlund Caspersen)
- `21b4a27b5` Turn list into headlines (Søren Bredlund Caspersen)
- `38edd1f81` Remove link to dnsseedpolicy (Søren Bredlund Caspersen)
- `94e17acfe` Fix links (Søren Bredlund Caspersen)
- `106c27319` Headers more pretty (Søren Bredlund Caspersen)
- `b5118cb41` Remove word "reference", link to README.md (Søren Bredlund Caspersen)
- `39e4303c0` Markdown formatting of headlines (Søren Bredlund Caspersen)
- `ebfc40303` make code stand out (Søren Bredlund Caspersen)
- `df559979c` Update to martdown syntax (Søren Bredlund Caspersen)
- `f34ebdabc` Delete dnsseed-policy.md (Søren Bredlund Caspersen)
- `0183a23e6` check if smId is attr before checking for nonetype. fixes "AttributeError: 'ScriptMachine' object has no attribute 'smId'" error (Greg-Griffith)
- `fca807cbc` Squashed 'src/univalue/' changes from 07947ff2da..51d3ab34ba (MarcoFalke)
- `f221ef4cf` Use UniValue.pushKV rather than push_back(Pair()) (Andrea Suisani)
- `3f863e3ec` Port Core #14164: Update univalue subtree (MarcoFalke)
- `cc7d30a9a` Squashed 'src/leveldb/' changes from 64052c76c5..524b7e36a8 (MarcoFalke)
- `9eca2e639` Squashed 'src/leveldb/' changes from c521b3ac65..64052c76c5 (MarcoFalke)
- `72411aa06` Fixed typo (Dimitris Tsapakidis)
- `10cc3fef8` Explicitly set py3 as the interpreter used by symbol-check.py (Andrea Suisani)
- `d3d7bb72e` [contrib] fixup symbol-check.py Python3 support (John Newbery)
- `6a18ed352` Add stdin, stdout, stderr to ignored export list (Chun Kuan Lee)
- `a02206956` Modified in_addr6 cast in AppInit2 to work with msvc. (Aaron Clauson)
- `47d0477dc` Use IN6ADDR_ANY_INIT instead of in6addr_any (Cory Fields)
- `8abc1db68` GCC-7 and glibc-2.27 compat code (Chun Kuan Lee)
- `507de8dc2` fix tsan: utiltime race on nMockTime (Pieter Wuille)
- `0acf2a3bb` Ditch boost scoped_ptr and use C++11 std::unique_ptr instead (Andrea Suisani)
- `6451acf4d` Remove cs_main from getblockcount(), GetDifficulty and getbestblockhash() (Peter Tschipper)
- `e4f2de739` Only check FindNextBlocksToDownload() if chain is not synced (Peter Tschipper)
- `bd81f3e26` Add missing lock: cs_objDownloader in FindNextBlocksToDownload() (Peter Tschipper)
- `a57fff093` Create  error(ctgr, ...) that allows us to specify  a logging category (#1560) (Peter Tschipper)
- `16623b54e` reduce number of nodes (resource usage) in getlogcategories.py (#1564) (Greg Griffith)
- `86efc7e25` [Windows Build Scripts] - Update toolchain and dependencies versions. (#1550) (Justaphf)
- `b568dc726` Update cleanup script to remove previous build outputs (#1540) (Justaphf)
- `188e02a6b` update dependency chart to reflect changes made in #1562 (Greg-Griffith)
- `4ed43e300` Simplify and update expedited code. (#1526) (Peter Tschipper)
- `b325bb089` Move to cxx 14 and bump boost min ver to match (#1562) (Greg Griffith)
- `eb780415a` Use canonical ordering in graphene (#1532) (bissias)
- `5cc36855c` Reduce log spam (#1561) (Peter Tschipper)
- `5a9cb53c4` Explicitly fold sub tasks in travis script step (Andrea Suisani)
- `f0018761c` Fix execution path in after_script and after_failure steps (Andrea Suisani)
- `91553fb25` travis: avoid timeout without saving caches, also enable qt (x86_64, arm) (Chun Kuan Lee)
- `8211bcacd` use export LC_ALL=C.UTF-8 (Julian Fleischer)
- `58b006bdf` Make script exit if a command fails (Julian Fleischer)
- `25b38c279` abort script in END_FOLD on non-zero exit code (Julian Fleischer)
- `d3302e79d` move script sections info individual files and comply with shellcheck (Julian Fleischer)
- `7e9f6b141` Update zmq to 4.3.1 (Dimitris Apostolou)
- `a73e73924` add /doc/Doxyfile to .gitignore (#1556) (Peter Tschipper)
- `48cde27c9`  Disable a debug log category if prefixed with '-'  (#1254) (Andrea Suisani)
- `6818d3f63` net_processing: Remove gotWorkDone variable (#1520) (awemany)
- `e62708a35` Remove version (#1546) (Søren Bredlund Caspersen)
- `42df4d80b` Refactor Script interpreter as a "virtual machine" encapsulated by a class.  Add py debugger (#1545) (Andrew Stone)
- `2d0b8a78f` Doxygen (#1552) (Søren Bredlund Caspersen)
- `46a1fb474` Properly initialize templates in Solver() (#1555) (Peter Tschipper)
- `2a7c9776f` Configure travis to use an Ubuntu 18.04 docker container  (#1450) (Andrea Suisani)
- `c9b8d675d` Modify clang-format.py to make it work with python 3.x (#1449) (Andrea Suisani)
- `9fa452ea1` Reduce scope of cs_main in ThreadCommitToMemPool() (#1434) (Peter Tschipper)
- `94a16ed44` implement xversion changeable key (#1503) (Greg Griffith)
- `9d4368c8a` Fix rescan=false not working in importprivkey (#1549) (Tom Harding)
- `90f5ce45f` Fix requestmanager_tests.cpp assertion (#1547) (Peter Tschipper)
- `736db440b` Improve optimization (#1525) (bissias)
- `5b52bb6df` Refactor tracking of thin type blocks in flight. (pre-req for compact blocks) (#1524) (Peter Tschipper)
- `9282407a7` fix behaviour now that thread_local is removed (Andrew Stone)
- `c0ae997a3` UI - Re-enable word wrap in peer details header (Justaphf)
- `bf4cabc8a` Add release notes for BUcash 1.5.1.0 (Andrea Suisani)
- `06081d918` Remove thread_local storage qualifier from logbuf declaration (Peter Tschipper)
- `77050bb2d` It seems we are ready to release final BUcash 1.5.1 (#1535) (Andrea Suisani)
- `820b8aabb` rpc: Prevent `dumpwallet` from overwriting files (Wladimir J. van der Laan)
- `119e8cb00` ppa update (#1529) (Søren Bredlund Caspersen)
- `867735f3b` Add a new parameter to define the duration of Graphene and Thinblock preferential timer. (#1531) (Andrea Suisani)
- `00324de78` Switch ctor on earlier in the init process if we are on the BCH chain (Andrea Suisani)
- `284176229` Remove a useless assert in PruneBlockIndexCandidates() (Andrea Suisani)
- `7c8e7220f` Add Graphene specification v1.0 (#1522) (Andrea Suisani)
- `960e00c2d` Improve Graphene decode rates by padding IBLT (#1426) (bissias)
- `7e0f47a7a` Extend CTransactionRef into a few more areas (#1472) (Peter Tschipper)
- `caed20d60` Proper multi-threaded multi-line logging and no more empty lines (#1441) (awemany)
- `8a4805c4a` zerochecksum: More comprehensive testing, test fix (awemany)
- `f9eb1aff8` mininode: Save exceptions for later evaluation (awemany)
- `8917f2e91` mininode: Allow to specify explicitly whether to send zero checksums (awemany)
- `dd819e58c` Move LoadMempool() and DumpMempool() to txmempool.cpp/.h (Peter Tschipper)
- `e32353ba4` Create a default constructor for CTxInputData and use it (Peter Tschipper)
- `f25f5cfff` fix format (Peter Tschipper)
- `5ed7834c9` Control mempool persistence using a command line parameter. (John Newbery)
- `6fe423b1d` Add savemempool RPC (Lawrence Nahum)
- `4db9b7954` Add return value to DumpMempool (Lawrence Nahum)
- `ebe3af4ec` Read/write mempool.dat as a binary. (Pavel Janík)
- `6977fb50b` [Qt] Do proper shutdown (Jonas Schnelli)
- `d57439041` Allow shutdown during LoadMempool, dump only when necessary (Jonas Schnelli)
- `9071de193` enqueue transactions when loading the mempool (Peter Tschipper)
- `c852f94b3` Add DumpMempool and LoadMempool (Pieter Wuille)
- `3b803db3a` Add mempool.dat to doc/files.md (Pieter Wuille)
- `5d39a3f0e` use uint32_t from stdint.h instead (Angel Leon)
- `61fec89a9` DbEnv constructor parameter explicit cast to avoid compilation error in macOS (gubatron)
- `8ecd596e9` Fix format (Peter Tschipper)
- `ef6f07c6d` Make code more readable (Peter Tschipper)
- `32434c88b` tests: Fix incorrect documentation for test case cuckoocache_hit_rate_ok (practicalswift)
- `beb05dd14` Use explicit casting in cuckoocache's compute_hashes(...) to clarify integer conversion (practicalswift)
- `98e03aa57` scripted-diff: Rename cuckoo tests' local rand context (Pieter Wuille)
- `72eb58d1e` Decrease testcase sizes in cuckoocache tests (Jeremy Rubin)
- `9cfb5eb30` Add unit tests for the CuckooCache (Jeremy Rubin)
- `efa32773c` add cuckoocache.h to .formatted-files (Peter Tschipper)
- `f0a9be828` Add CuckooCache implementation and replace the sigcache map_type with it (Jeremy Rubin)

Credits
=======

Thanks to everyone who directly contributed to this release:

- Andrea Suisani
- Andrew Stone
- Angel Leon
- awemany
- Dagur Valberg
- Dimitris Tsapakidis
- George Bissias
- Greg Griffith
- gubatron
- Justaphf
- Peter Tschipper
- romanornr
- Søren B. Caspersen
- Tom Harding

We have backported an amount of changes from other projects, namely Bitcoin Core, Bitcoin ABC and Bitcoin SV.

Following all the indirect contributors whose work has been imported via the above backports:

- Amaury Séchet
- Aaron Clauson
- Chun Kuan Lee
- Cory Fields
- Fabien
- Florian
- fanquake
- Fuzzbawls
- Jeremy Rubin
- John Murphy
- John Newbery
- Jonas Schnelli
- Jorge Timón
- Julian Fleischer
- Ken Lee
- Marco Falke
- Mark Lundeberg
- Matt Corallo
- Pavel Janík
- Pieter Wuille
- practicalswift
- Russell Yanofsky
- Sebastian Kung
- Sjors Provoost
- TheCharlatan
- Wladimir J. van der Laan

