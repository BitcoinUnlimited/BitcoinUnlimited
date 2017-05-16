Generalized version bits voting (BIP135)
================================================

1. What is this?
------------------------

BIP135 is a reworked version of BIP9 versionbits which allows each
versionbit to be configured with its own threshold etc.


2. Requirements
------------------------

The draft specification can be found at:

https://github.com/bitcoin/bips/blob/master/bip-0135.mediawiki

Some formal requirements have been extracted into the file

doc/bip135-genvbvoting-condensed-requirements.txt

These are intended to be integrated back into an updated version of the BIP.


3. Design
------------------------

3.1. Config file
~~~~~~~~~~~~~~~~~~~~~~~~

Fork (deployment) information can be read in from a configuration file
(if available) for ease of maintenance and regression testing.

This new configuration file, forks.csv, will override the built-in client
defaults if it is present in the datadir or at the path specified by the
`-forks=<filepath>` parameter.

The format of forks.csv is comma-separated value (CSV, RFC4180 [1]).
It contains the versionbits configuration for each network known to the
client (matched using the chains' strNetworkID defined in chainparams.cpp).

File format:
Lines beginning with hashes or semicolons will be treated as comment lines and
ignored.
Data lines will consist of the following comma-separated fields:

    network,bit,name,starttime,timeout,windowsize,threshold,minlockedblocks,minlockedtime,gbtforce

The expected data types of these fields are:

    network         - ASCII string
    bit             - integer in range 0..28
    name            - ASCII string
    starttime       - integer representing UNIX (POSIX) timestamp
    timeout         - integer representing UNIX (POSIX) timestamp
    windowsize      - positive integer > 1
    threshold       - positive integer >= 1 and <= windowsize
    minlockedblocks - integer >= 0
    minlockedtime   - integer number of seconds >= 0
    gbtforce        - boolean (true/false)

Example of file content (with header comment):

    # forks.csv
    # This file defines the known consensus changes tracked by the software
    # MODIFY AT OWN RISK - EXERCISE EXTREME CARE
    # Line format:
    # network,bit,name,starttime,timeout,windowsize,threshold,minlockedblocks,minlockedtime,gbtforce
    # main network, 95% @ 2016 blocks:
    main,0,csv,1462060800,1493596800,2016,1916,2016,0,true
    main,1,segwit,1479168000,1510704000,2016,1916,2016,0,true
    main,28,testdummy,1199145601,1230767999,2016,1916,2016,0,true
    ; test network (testnet), 75% @ 2016 blocks:
    test,0,csv,1456790400,1493596800,2016,1512,2016,0,true
    test,1,segwit,1462060800,1493596800,2016,1512,2016,0,true
    test,28,testdummy,1199145601,1230767999,2016,1512,2016,0,true
    ; regtest chain 75% @ 144 blocks:
    regtest,0,csv,0,999999999999,144,108,108,0,true
    regtest,1,segwit,0,999999999999,144,108,108,0,true
    regtest,28,testdummy,0,999999999999,144,108,108,0,true

Bits that are not defined shall not be listed.
The 'testdummy' assignments on bit 28 are historically used by some tests.

The built-in defaults can be dumped out in CSV format (suitable for creating
a forks.csv) by running bitcoind with the `-dumpforks` option. This will dump
the data and terminate the client.

A sample 'forks.csv' has been committed in config/forks.csv .
Currently this is not installed, since the client's built-in defaults
make it unnecessary for a forks.csv file to be deployed unless the
user wishes to modify deployments. Typically though, this will be
something that developers do rather than users, until they have tested
their new deployment settings and transfer them to built-in settings.

CSV file validation errors at startup lead to termination of the client
with error message on console and more detailed messages in the log file.
This is on purpose, to prevent operation on possibly incomplete / bad data.


3.2. Adaptated files
~~~~~~~~~~~~~~~~~~~~~~~~

The AbstractThresholdConditionChecker has been extended with the
necessary extra parameters and state transition adaptations to comply
with the BIP (in versionbits.{h,cpp}.

The 'global' threshold and window parameters have been removed from
the chain parameters, and replaced by per-deployment default settings
in chainparams.{h,cpp}.

The DeploymentPos enum list in consensus/params.h has been extended
with explicit DEPLOYMENT_UNASSIGNED_BIT_x values, and some instructions
close by on how to manage these when introducing new deployments.

init.cpp has been adapted to display help for the two new options
(-forks=<filepath> and -dumpforks), and to call the CSV reading
procedure (refer to the new module forks_csv.{h,cpp} and its
tests in forkscsv_tests.cpp).

The "unknown versions" test in main.cpp had to be ripped out and
replaced by a new test, and subsequently p2p-versionbits-warning.py
had to be revised as well.

The new unknown versions check still reports the same error,
but at 51/100 unknown versions. It makes no more assumptions over
activation states of the unknown deployments.
It reports when a new signal is first detected, and reports it
at 25, 50, 70, 90 and 95 percent, and also if it becomes lost
again (all this is counted over the last 100 blocks).
There are information log messages for these events in the debug.log,
but no warnings / RPC error messages, to keep this reference
implementation small.

A new RPC output section has been added to the `getblockchaininfo`
call. It is called 'bip135_forks', and complements the existing
section 'bip9_softforks', which has been retained for compatibility
but can be made obsolete at a later stage.
The new section lists all parameter values and the bit number for
each 'configured' deployment - which means those which have a name
and valid settings (i.e. nonzero window size etc).

This RPC interface is also used by the new regression test
for this BIP, which is bip135-genvbvoting-forks.py .
The old bip9-softforks.py is retained to prove backward compatibility.
The same method has been followed for unit tests:
The old versionbits_tests.cpp has been retained mostly unchanged -
only minimal necessary adaptations where needed to compile, and
a fixup for the time period disjointness subtest which was broken.
The new functionality is unit tested in genversionbits_tests.cpp.

The CSV reading is done by a new vendor package which has been
pulled into the project as a subtree (see src/fast-cpp-csv-parser/).


4. Test plan
------------------------

4.1 Unit tests
~~~~~~~~~~~~~~~~~~~~~~~~

Unit tests have been added for the adapted and new classes (e.g. state
machine, reading and validating contents of the forks.csv file).

4.2 Regression tests
~~~~~~~~~~~~~~~~~~~~~~~~

The bip135-genvbvoting-forks.py test exercises a variety of settings on bits 1-21.

The existing regression tests sufficiently test the timeout transitions of
BIP9 which remain compatible.


5. Recommendations received during spec review:
--------------------------------------------------------

Some observations on review comments received privately (outside of main review
list and forums) noted here with thoughts on implementation:

5.1 Be self-documenting
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This is achieved by a command line argument (`-dumpforks`) which causes the
client to dump its built-in default deployment data in CSV format, ready to
be used as a template for a forks.csv file.

The output contains a standard commented file header similar to the above
example, so that a user can immediately know the meaning of the fields.

5.2 Be upgrade-ready
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The default mode is still to deliver built-in defaults corresponding to the
known deployments. An upgrade works just like today - install a newer client
which comes with updated deployment built-in.

For comfort, packaging scripts could generate the latest forks.csv matching
the built-in defaults, and place that in an informational folder where it
is available for reference. This has not been done in this reference
implementation as it involves mostly platform specific steps which are not
directly related to the functionality, and are purely for convenience.

Install scripts should, when upgrading, check for the presence of an active
forks.csv file (e.g. in the datadir) and warn the user in case there are
differences that might need merging.

5.3 Don't leave crappy confusing files on anyone's computer
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

No new files are generated automatically during runtime, this data only
needs to be read by the client.

Uninstall scripts should remove any informational copies of the configuration
file, and leave the user's active file (if present) untouched unless the
user opts to remove them during de-installation.


References
------------------------

[1] https://tools.ietf.org/rfc/rfc4180.txt
