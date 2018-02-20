Developer Notes
===============

Formatting
-----------

Various coding styles have been used during the history of the codebase,
and the result is not very consistent. However, we're now trying to converge to
a single style, so please use it in new code. Old code will be converted
gradually.
- Basic rules are specified in src/.clang-format. See below for scripts to format automatically.
  - "Allman" indentation: Braces on new lines for namespaces, classes, functions, methods, code blocks.
  - 4 space indentation (no tabs) for every block except namespaces.
  - No indentation for public/protected/private or for namespaces.
  - No extra spaces inside parenthesis; don't do ( this )
  - No space after function names; one space after if, for and while.
  - 120 character column limit

Block style example:
```c++
namespace foo
{
class Class
{
    bool Function(char* psz, int n)
    {
        // Comment summarising what this section of code does
        for (int i = 0; i < n; i++)
        {
            // When something fails, return early
            if (!Something())
                return false;
            ...
        }

        // Success return is usually at the end
        return true;
    }
}
}
```

- Strict formatting is enforced all files listed in "src/.formatted-files".
  Your pull request will fail the Travis build if one of these files is not formatted properly

- To auto-format a file:

  The fully-automatic way:
  ========================
  Add the following to your $HOME/.gitconfig or .git/config in the Bitcoin directory:
  ```
[filter "bitcoin-clang-format"]
        clean = "contrib/devtools/clang-format.py format-stdout-if-wanted clang-format-3.8 %f"
        smudge = cat
  ```

  The semi-automatic way:
  =======================
   Formatting happens in two passes.  First a python script is run that finds trailing comments that exceed 120 characters
   and moves them up to the prior line.  Second clang-format-3.8 is executed.

   The first step is to install clang-format.  You likely need to install this exact version or formatting may be slightly different.
```bash
sudo apt-get install clang-format-3.8
```

   Next, run the wrapper script to format your files:

```bash
./contrib/devtools/clang-format.py format clang-format-3.8 <filenames...>
```
   Do not fix other people's formatting issues in your PR, since it distracts from the content!  Let the BitcoinUnlimitedJanitor clean up other people's mess.


- To check proper formatting of a file:
```bash
./contrib/devtools/clang-format.py check clang-format-3.8 <filenames...>
```

- To check that your formatting will pass Travis:
```bash
make check-formatting
```

- Formatting style for emacs (add this to your .elisp file).  You can also find a full-featured emacs environment [here](https://github.com/gandrewstone/BUtools).
```elisp
;; Indentation style and number of spaces
(setq c-default-style "bsd"
      c-basic-offset 4)

;; Use spaces for a tab.
(setq c-tab-always-indent nil)
(setq-default indent-tabs-mode nil)
(setq tab-stop-list '(4 8 12 16 20 22 24 26 28 30 32 34 36 38 40 42 44 46 48 50))
(setq standard-indent 1)

;; Highlight a lot of style issues
(require 'whitespace)
(setq whitespace-line-column 120) ;; limit line length to 120 chars
(setq whitespace-style '(face lines-tail trailing tabs))
(add-hook 'prog-mode-hook 'whitespace-mode)

;; Delete trailing whitespace on save
;;   Be careful that this does not cause a lot of unrelated changes.
;;   You may need to comment this out when editing some files
(add-hook 'c-mode-hook
                (lambda () (add-to-list 'write-file-functions 'delete-trailing-whitespace)))

;; Import clang-format functionality into emacs (ctrl-tab to format the selected region)
(load "clang-format-3.8/clang-format.el")
(global-set-key [C-tab] 'clang-format-region)
```


Doxygen comments
-----------------

To facilitate the generation of documentation, use doxygen-compatible comment blocks for functions, methods and fields.

For example, to describe a function use:
```c++
/**
 * ... text ...
 * @param[in] arg1    A description
 * @param[in] arg2    Another argument description
 * @pre Precondition for function...
 */
bool function(int arg1, const char *arg2)
```
A complete list of `@xxx` commands can be found at http://www.stack.nl/~dimitri/doxygen/manual/commands.html.
As Doxygen recognizes the comments by the delimiters (`/**` and `*/` in this case), you don't
*need* to provide any commands for a comment to be valid; just a description text is fine.

To describe a class use the same construct above the class definition:
```c++
/**
 * Alerts are for notifying old versions if they become too obsolete and
 * need to upgrade. The message is displayed in the status bar.
 * @see GetWarnings()
 */
class CAlert
{
```

To describe a member or variable use:
```c++
int var; //!< Detailed description after the member
```

Also OK:
```c++
///
/// ... text ...
///
bool function2(int arg1, const char *arg2)
```

Not OK (used plenty in the current source, but not picked up):
```c++
//
// ... text ...
//
```

A full list of comment syntaxes picked up by doxygen can be found at http://www.stack.nl/~dimitri/doxygen/manual/docblocks.html,
but if possible use one of the above styles.

Development tips and tricks
---------------------------

**compiling for debugging**

Run configure with the --enable-debug option, then make. Or run configure with
CXXFLAGS="-g -ggdb -O0" or whatever debug flags you need.

**debug.log**

If the code is behaving strangely, take a look in the debug.log file in the data directory;
error and debugging messages are written there.

The -debug=... command-line option controls debugging; running with just -debug or -debug=1 will turn
on all categories (and give you a very large debug.log file).

The Qt code routes qDebug() output to debug.log under category "qt": run with -debug=qt
to see it.

**testnet and regtest modes**

Run with the -testnet option to run with "play bitcoins" on the test network, if you
are testing multi-machine code that needs to operate across the internet.

If you are testing something that can run on one machine, run with the -regtest option.
In regression test mode, blocks can be created on-demand; see qa/rpc-tests/ for tests
that run in -regtest mode.

**DEBUG_LOCKORDER**

Bitcoin Unlimited is a multithreaded application, and deadlocks or other multithreading bugs
can be very difficult to track down. Compiling with -DDEBUG_LOCKORDER (configure
CXXFLAGS="-DDEBUG_LOCKORDER -g") inserts run-time checks to keep track of which locks
are held, and adds warnings to the debug.log file if inconsistencies are detected.

**Memory Profiling**

*Currently only available on Linux*

Bitcoin Unlimited can be compiled with the libtcmalloc allocation library and
memory profiling tool.  First install libtcmalloc either from source here
https://github.com/gperftools/gperftools or via package manager:
```bash
sudo apt-get install libgoogle-perftools-dev
```
Next reconfigure:
```bash
make distclean
./configure --enable-gperf --disable-hardening --enable-debug
```
For detailed instructions on how to use gperftools please read the gperftools
documentation.


Locking/mutex usage notes
-------------------------

The code is multi-threaded, and uses mutexes and the
LOCK/TRY_LOCK macros to protect data structures.

Deadlocks due to inconsistent lock ordering (thread 1 locks cs_main
and then cs_wallet, while thread 2 locks them in the opposite order:
result, deadlock as each waits for the other to release its lock) are
a problem. Compile with -DDEBUG_LOCKORDER to get lock order
inconsistencies reported in the debug.log file.

Re-architecting the core code so there are better-defined interfaces
between the various components is a goal, with any necessary locking
done by the components (e.g. see the self-contained CKeyStore class
and its cs_KeyStore lock for example).

Threads
-------

- ThreadScriptCheck : Verifies block scripts.

- ThreadImport : Loads blocks from blk*.dat files or bootstrap.dat.

- StartNode : Starts other threads.

- ThreadBitnodesAddressSeed : Runs initial bitnodes seeding when no peers.dat exists.

- ThreadDNSAddressSeed : Loads addresses of peers from the DNS.

- ThreadMapPort : Universal plug-and-play startup/shutdown

- ThreadSocketHandler : Sends/Receives data from peers on port 8333.

- ThreadOpenAddedConnections : Opens network connections to added nodes.

- ThreadOpenConnections : Initiates new connections to peers.

- ThreadMessageHandler : Higher-level message handling (sending and receiving).

- DumpAddresses : Dumps IP addresses of nodes to peers.dat.

- ThreadFlushWalletDB : Close the wallet.dat file if it hasn't been used in 500ms.

- ThreadRPCServer : Remote procedure call handler, listens on port 8332 for connections and services them.

- BitcoinMiner : Generates bitcoins (if wallet is enabled).

- Shutdown : Does an orderly shutdown of everything.

Ignoring IDE/editor files
--------------------------

In closed-source environments in which everyone uses the same IDE it is common
to add temporary files it produces to the project-wide `.gitignore` file.

However, in open source software such as Bitcoin Unlimited, where everyone uses
their own editors/IDE/tools, it is less common. Only you know what files your
editor produces and this may change from version to version. The canonical way
to do this is thus to create your local gitignore. Add this to `~/.gitconfig`:

```
[core]
        excludesfile = /home/.../.gitignore_global
```

(alternatively, type the command `git config --global core.excludesfile ~/.gitignore_global`
on a terminal)

Then put your favourite tool's temporary filenames in that file, e.g.
```
# NetBeans
nbproject/
```

Another option is to create a per-repository excludes file `.git/info/exclude`.
These are not committed but apply only to one repository.

If a set of tools is used by the build system or scripts the repository (for
example, lcov) it is perfectly acceptable to add its files to `.gitignore`
and commit them.

Development guidelines
============================

A few non-style-related recommendations for developers, as well as points to
pay attention to for reviewers of Bitcoin Unlimited code.

General Bitcoin Unlimited
----------------------

- New features should be exposed on RPC first, then can be made available in the GUI

  - *Rationale*: RPC allows for better automatic testing. The test suite for
    the GUI is very limited

- Make sure pull requests pass Travis CI before merging

  - *Rationale*: Makes sure that they pass thorough testing, and that the tester will keep passing
     on the master branch. Otherwise all new pull requests will start failing the tests, resulting in
     confusion and mayhem
 
  - *Explanation*: If the test suite is to be updated for a change, this has to
    be done first 

Wallet
-------

- Make sure that no crashes happen with run-time option `-disablewallet`.

  - *Rationale*: In RPC code that conditionally uses the wallet (such as
    `validateaddress`) it is easy to forget that global pointer `pwalletMain`
    can be NULL. See `qa/rpc-tests/disablewallet.py` for functional tests
    exercising the API with `-disablewallet`

- Include `db_cxx.h` (BerkeleyDB header) only when `ENABLE_WALLET` is set

  - *Rationale*: Otherwise compilation of the disable-wallet build will fail in environments without BerkeleyDB

General C++
-------------

- Assertions should not have side-effects

  - *Rationale*: Even though the source code is set to to refuse to compile
    with assertions disabled, having side-effects in assertions is unexpected and
    makes the code harder to understand

- If you use the `.h`, you must link the `.cpp`

  - *Rationale*: Include files define the interface for the code in implementation files. Including one but
      not linking the other is confusing. Please avoid that. Moving functions from
      the `.h` to the `.cpp` should not result in build errors

- Use the RAII (Resource Acquisition Is Initialization) paradigm where possible. For example by using
  `scoped_pointer` for allocations in a function.

  - *Rationale*: This avoids memory and resource leaks, and ensures exception safety

C++ data structures
--------------------

- Never use the `std::map []` syntax when reading from a map, but instead use `.find()`

  - *Rationale*: `[]` does an insert (of the default element) if the item doesn't
    exist in the map yet. This has resulted in memory leaks in the past, as well as
    race conditions (expecting read-read behavior). Using `[]` is fine for *writing* to a map

- Do not compare an iterator from one data structure with an iterator of
  another data structure (even if of the same type)

  - *Rationale*: Behavior is undefined. In C++ parlor this means "may reformat
    the universe", in practice this has resulted in at least one hard-to-debug crash bug

- Watch out for vector out-of-bounds exceptions. `&vch[0]` is illegal for an
  empty vector, `&vch[vch.size()]` is always illegal. Use `begin_ptr(vch)` and
  `end_ptr(vch)` to get the begin and end pointer instead (defined in
  `serialize.h`)

- Vector bounds checking is only enabled in debug mode. Do not rely on it

- Make sure that constructors initialize all fields. If this is skipped for a
  good reason (i.e., optimization on the critical path), add an explicit
  comment about this

  - *Rationale*: Ensure determinism by avoiding accidental use of uninitialized
    values. Also, static analyzers balk about this.

- Use explicitly signed or unsigned `char`s, or even better `uint8_t` and
  `int8_t`. Do not use bare `char` unless it is to pass to a third-party API.
  This type can be signed or unsigned depending on the architecture, which can
  lead to interoperability problems or dangerous conditions such as
  out-of-bounds array accesses

- Prefer explicit constructions over implicit ones that rely on 'magical' C++ behavior

  - *Rationale*: Easier to understand what is happening, thus easier to spot mistakes, even for those
  that are not language lawyers

Strings and formatting
------------------------

- Be careful of `LogPrint` versus `LogPrintf`. `LogPrint` takes a `category` argument, `LogPrintf` does not.

  - *Rationale*: Confusion of these can result in runtime exceptions due to
    formatting mismatch, and it is easy to get wrong because of subtly similar naming

- Use `std::string`, avoid C string manipulation functions

  - *Rationale*: C++ string handling is marginally safer, less scope for
    buffer overflows and surprises with `\0` characters. Also some C string manipulations
    tend to act differently depending on platform, or even the user locale

- Use `ParseInt32`, `ParseInt64`, `ParseDouble` from `utilstrencodings.h` for number parsing

  - *Rationale*: These functions do overflow checking, and avoid pesky locale issues

- For `strprintf`, `LogPrint`, `LogPrintf` formatting characters don't need size specifiers

  - *Rationale*: Bitcoin Unlimited uses tinyformat, which is type safe. Leave them out to avoid confusion

Threads and synchronization
----------------------------

- Build and run tests with `-DDEBUG_LOCKORDER` to verify that no potential
  deadlocks are introduced. As of 0.12, this is defined by default when
  configuring with `--enable-debug`

- When using `LOCK`/`TRY_LOCK` be aware that the lock exists in the context of
  the current scope, so surround the statement and the code that needs the lock
  with braces

  OK:

```c++
{
    TRY_LOCK(cs_vNodes, lockNodes);
    ...
}
```

  Wrong:

```c++
TRY_LOCK(cs_vNodes, lockNodes);
{
    ...
}
```

Source code organization
--------------------------

- Implementation code should go into the `.cpp` file and not the `.h`, unless necessary due to template usage or
  when performance due to inlining is critical

  - *Rationale*: Shorter and simpler header files are easier to read, and reduce compile time

- Don't import anything into the global namespace (`using namespace ...`). Use
  fully specified types such as `std::string`.

  - *Rationale*: Avoids symbol conflicts

GUI
-----

- Do not display or manipulate dialogs in model code (classes `*Model`)

  - *Rationale*: Model classes pass through events and data from the core, they
    should not interact with the user. That's where View classes come in. The converse also
    holds: try to not directly access core data structures from Views.

Subtrees
----------

Several parts of the repository are subtrees of software maintained elsewhere.

Some of these are maintained by active developers of Bitcoin Core, in which case changes should probably go
directly upstream without being PRed directly against the project.  They will be merged back in the next
subtree merge.

Others are external projects without a tight relationship with our project.  Changes to these should also
be sent upstream but bugfixes may also be prudent to PR against Bitcoin Core so that they can be integrated
quickly.  Cosmetic changes should be purely taken upstream.

There is a tool in contrib/devtools/git-subtree-check.sh to check a subtree directory for consistency with
its upstream repository.

Current subtrees include:

- src/leveldb
  - Upstream at https://github.com/google/leveldb ; Maintained by Google, but
    open important PRs to Core to avoid delay.
  - **Note**: Follow the instructions in [Upgrading LevelDB](#upgrading-leveldb) when
    merging upstream changes to the leveldb subtree.

- src/libsecp256k1
  - Upstream at https://github.com/bitcoin-core/secp256k1/ ; actively maintaned by Core contributors.

- src/crypto/ctaes
  - Upstream at https://github.com/bitcoin-core/ctaes ; actively maintained by Core contributors.

- src/univalue
  - Upstream at https://github.com/jgarzik/univalue ; report important PRs to Core to avoid delay.

Upgrading LevelDB
---------------------

Extra care must be taken when upgrading LevelDB. This section explains issues
you must be aware of.

### File Descriptor Counts

In most configurations we use the default LevelDB value for `max_open_files`,
which is 1000 at the time of this writing. If LevelDB actually uses this many
file descriptors it will cause problems with Bitcoin's `select()` loop, because
it may cause new sockets to be created where the fd value is >= 1024. For this
reason, on 64-bit Unix systems we rely on an internal LevelDB optimization that
uses `mmap()` + `close()` to open table files without actually retaining
references to the table file descriptors. If you are upgrading LevelDB, you must
sanity check the changes to make sure that this assumption remains valid.

In addition to reviewing the upstream changes in `env_posix.cc`, you can use `lsof` to
check this. For example, on Linux this command will show open `.ldb` file counts:

```bash
$ lsof -p $(pidof bitcoind) |\
    awk 'BEGIN { fd=0; mem=0; } /ldb$/ { if ($4 == "mem") mem++; else fd++ } END { printf "mem = %s, fd = %s\n", mem, fd}'
mem = 119, fd = 0
```

The `mem` value shows how many files are mmap'ed, and the `fd` value shows you
many file descriptors these files are using. You should check that `fd` is a
small number (usually 0 on 64-bit hosts).

See the notes in the `SetMaxOpenFiles()` function in `dbwrapper.cc` for more
details.

### Consensus Compatibility

It is possible for LevelDB changes to inadvertently change consensus
compatibility between nodes. This happened in Bitcoin 0.8 (when LevelDB was
first introduced). When upgrading LevelDB you should review the upstream changes
to check for issues affecting consensus compatibility.

For example, if LevelDB had a bug that accidentally prevented a key from being
returned in an edge case, and that bug was fixed upstream, the bug "fix" would
be an incompatible consensus change. In this situation the correct behavior
would be to revert the upstream fix before applying the updates to Bitcoin's
copy of LevelDB. In general you should be wary of any upstream changes affecting
what data is returned from LevelDB queries.

Git and GitHub tips
---------------------

- For resolving merge/rebase conflicts, it can be useful to enable diff3 style using
  `git config merge.conflictstyle diff3`. Instead of

        <<<
        yours
        ===
        theirs
        >>>

  you will see

        <<<
        yours
        |||
        original
        ===
        theirs
        >>>

  This may make it much clearer what caused the conflict. In this style, you can often just look
  at what changed between *original* and *theirs*, and mechanically apply that to *yours* (or the other way around).

- When reviewing patches which change indentation in C++ files, use `git diff -w` and `git show -w`. This makes
  the diff algorithm ignore whitespace changes. This feature is also available on github.com, by adding `?w=1`
  at the end of any URL which shows a diff.

- When reviewing patches that change symbol names in many places, use `git diff --word-diff`. This will instead
  of showing the patch as deleted/added *lines*, show deleted/added *words*.

- When reviewing patches that move code around, try using
  `git diff --patience commit~:old/file.cpp commit:new/file/name.cpp`, and ignoring everything except the
  moved body of code which should show up as neither `+` or `-` lines. In case it was not a pure move, this may
  even work when combined with the `-w` or `--word-diff` options described above.

- When looking at other's pull requests, it may make sense to add the following section to your `.git/config`
  file:

        [remote "upstream-pull"]
                fetch = +refs/pull/*:refs/remotes/upstream-pull/*
                url = git@github.com:bitcoin/bitcoin.git

  This will add an `upstream-pull` remote to your git repository, which can be fetched using `git fetch --all`
  or `git fetch upstream-pull`. Afterwards, you can use `upstream-pull/NUMBER/head` in arguments to `git show`,
  `git checkout` and anywhere a commit id would be acceptable to see the changes from pull request NUMBER.

Scripted diffs
--------------

For reformatting and refactoring commits where the changes can be easily automated using a bash script, we use
scripted-diff commits. The bash script is included in the commit message and our Travis CI job checks that
the result of the script is identical to the commit. This aids reviewers since they can verify that the script
does exactly what it's supposed to do. It is also helpful for rebasing (since the same script can just be re-run
on the new master commit).

To create a scripted-diff:

- start the commit message with `scripted-diff:` (and then a description of the diff on the same line)
- in the commit message include the bash script between lines containing just the following text:
    - `-BEGIN VERIFY SCRIPT-`
    - `-END VERIFY SCRIPT-`

The scripted-diff is verified by the tool `contrib/devtools/commit-script-check.sh`

Commit [`bb81e173`](https://github.com/bitcoin/bitcoin/commit/bb81e173) is an example of a scripted-diff.

RPC interface guidelines
--------------------------

A few guidelines for introducing and reviewing new RPC interfaces:

- Method naming: use consecutive lower-case names such as `getrawtransaction` and `submitblock`

  - *Rationale*: Consistency with existing interface.

- Argument naming: use snake case `fee_delta` (and not, e.g. camel case `feeDelta`)

  - *Rationale*: Consistency with existing interface.

- Use the JSON parser for parsing, don't manually parse integers or strings from
  arguments unless absolutely necessary.

  - *Rationale*: Introduces hand-rolled string manipulation code at both the caller and callee sites,
    which is error prone, and it is easy to get things such as escaping wrong.
    JSON already supports nested data structures, no need to re-invent the wheel.

  - *Exception*: AmountFromValue can parse amounts as string. This was introduced because many JSON
    parsers and formatters hard-code handling decimal numbers as floating point
    values, resulting in potential loss of precision. This is unacceptable for
    monetary values. **Always** use `AmountFromValue` and `ValueFromAmount` when
    inputting or outputting monetary values. The only exceptions to this are
    `prioritisetransaction` and `getblocktemplate` because their interface
    is specified as-is in BIP22.

- Missing arguments and 'null' should be treated the same: as default values. If there is no
  default value, both cases should fail in the same way. The easiest way to follow this
  guideline is detect unspecified arguments with `params[x].isNull()` instead of
  `params.size() <= x`. The former returns true if the argument is either null or missing,
  while the latter returns true if is missing, and false if it is null.

  - *Rationale*: Avoids surprises when switching to name-based arguments. Missing name-based arguments
  are passed as 'null'.

- Try not to overload methods on argument type. E.g. don't make `getblock(true)` and `getblock("hash")`
  do different things.

  - *Rationale*: This is impossible to use with `bitcoin-cli`, and can be surprising to users.

  - *Exception*: Some RPC calls can take both an `int` and `bool`, most notably when a bool was switched
    to a multi-value, or due to other historical reasons. **Always** have false map to 0 and
    true to 1 in this case.

- Don't forget to fill in the argument names correctly in the RPC command table.

  - *Rationale*: If not, the call can not be used with name-based arguments.

- Set okSafeMode in the RPC command table to a sensible value: safe mode is when the
  blockchain is regarded to be in a confused state, and the client deems it unsafe to
  do anything irreversible such as send. Anything that just queries should be permitted.

  - *Rationale*: Troubleshooting a node in safe mode is difficult if half the
    RPCs don't work.

- Add every non-string RPC argument `(method, idx, name)` to the table `vRPCConvertParams` in `rpc/client.cpp`.

  - *Rationale*: `bitcoin-cli` and the GUI debug console use this table to determine how to
    convert a plaintext command line to JSON. If the types don't match, the method can be unusable
    from there.

- A RPC method must either be a wallet method or a non-wallet method. Do not
  introduce new methods such as `signrawtransaction` that differ in behavior
  based on presence of a wallet.

  - *Rationale*: as well as complicating the implementation and interfering
    with the introduction of multi-wallet, wallet and non-wallet code should be
    separated to avoid introducing circular dependencies between code units.

- Try to make the RPC response a JSON object.

  - *Rationale*: If a RPC response is not a JSON object then it is harder to avoid API breakage if
    new data in the response is needed.

- Wallet RPCs call BlockUntilSyncedToCurrentChain to maintain consistency with
  `getblockchaininfo`'s state immediately prior to the call's execution. Wallet
  RPCs whose behavior does *not* depend on the current chainstate may omit this
  call.

  - *Rationale*: In previous versions of Bitcoin Core, the wallet was always
    in-sync with the chainstate (by virtue of them all being updated in the
    same cs_main lock). In order to maintain the behavior that wallet RPCs
    return results as of at least the highest best-known block an RPC
    client may be aware of prior to entering a wallet RPC call, we must block
    until the wallet is caught up to the chainstate as of the RPC call's entry.
    This also makes the API much easier for RPC clients to reason about.

- Be aware of RPC method aliases and generally avoid registering the same
  callback function pointer for different RPCs.

  - *Rationale*: RPC methods registered with the same function pointer will be
    considered aliases and only the first method name will show up in the
    `help` rpc command list.

  - *Exception*: Using RPC method aliases may be appropriate in cases where a
    new RPC is replacing a deprecated RPC, to avoid both RPCs confusingly
    showing up in the command list.
