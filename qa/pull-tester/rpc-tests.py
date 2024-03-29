#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Copyright (c) 2015-2017 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Run Regression Test Suite

This module calls down into individual test cases via subprocess. It will
forward all unrecognized arguments onto the individual test scripts, other
than:

    - `-h` or '--help': print help about all options
    - `-extended`: run the "extended" test suite in addition to the basic one.
    - `-extended-only`: run ONLY the "extended" test suite
    - `-list`: only list the test scripts, do not run. Works in combination
      with '-extended' and '-extended-only' too, to print subsets.
    - `-win`: signal that this is running in a Windows environment, and we
      should run the tests.
    - `--coverage`: this generates a basic coverage report for the RPC
      interface.

For more detailed help on options, run with '--help'.

For a description of arguments recognized by test scripts, see
`qa/pull-tester/test_framework/test_framework.py:BitcoinTestFramework.main`.

"""
import pdb
import os
import time
import shutil
import psutil
import signal
import sys
import subprocess
import tempfile
import re

# to support out-of-source builds, we need to add both the source directory to the path, and the out-of-source directory
# because tests_config is a generated file
sourcePath = os.path.dirname(os.path.realpath(__file__))
outOfSourceBuildPath = os.path.dirname(os.path.abspath(__file__))
sys.path.append(sourcePath)
if sourcePath != outOfSourceBuildPath:
    sys.path.append(outOfSourceBuildPath)

from tests_config import *
from test_classes import RpcTest, Disabled, Skip, WhenElectrumFound

def inTravis():
    return (os.environ.get("TRAVIS", None) == "true")

def inGitLabCI():
    # https://docs.gitlab.com/ee/ci/variables/
    return (os.environ.get("CI_SERVER", None) == "yes")


BOLD = ("","")
if os.name == 'posix':
    # primitive formatting on supported
    # terminal via ANSI escape sequences:
    BOLD = ('\033[0m', '\033[1m')

RPC_TESTS_DIR = SRCDIR + '/qa/rpc-tests/'

CORE_ANALYSIS_SCRIPT = SRCDIR + '/contrib/devtools/coreanalysis.gdb'

#If imported values are not defined then set to zero (or disabled)
if 'ENABLE_WALLET' not in vars():
    ENABLE_WALLET=0
if 'ENABLE_BITCOIND' not in vars():
    ENABLE_BITCOIND=0
if 'ENABLE_UTILS' not in vars():
    ENABLE_UTILS=0
if 'ENABLE_ZMQ' not in vars():
    ENABLE_ZMQ=0

ENABLE_COVERAGE=0

#Create a set to store arguments and create the passOn string
opts = set()
double_opts = set()  # BU: added for checking validity of -- opts
passOn = ""
showHelp = False  # if we need to print help
p = re.compile("^--")
p_parallel = re.compile('^-parallel=')
run_parallel = 4

# some of the single-dash options applicable only to this runner script
# are also allowed in double-dash format (but are not passed on to the
# test scripts themselves)
private_single_opts = ('-h',
                       '-f',    # equivalent to -force-enable
                       '-help',
                       '-list',
                       '-extended',
                       '-extended-only',
                       '-electrum-only',
                       '-only-extended',
                       '-only-electrum',
                       '-force-enable',
                       '-win')
private_double_opts = ('--list',
                       '--extended',
                       '--extended-only',
                       '--electrum-only',
                       '--only-extended',
                       '--only-electrum',
                       '--force-enable',
                       '--win')
framework_opts = ('--tracerpc',
                  '--help',
                  '--noshutdown',
                  '--nocleanup',
                  '--no-ipv6-rpc-listen',
                  '--gitlab',
                  '--srcdir',
                  '--tmppfx',
                  '--coveragedir',
                  '--randomseed',
                  '--testbinary',
                  '--refbinary')
test_script_opts = ('--mineblock',
                    '--extensive')

def option_passed(option_without_dashes):
    """check if option was specified in single-dash or double-dash format"""
    return ('-' + option_without_dashes in opts
            or '--' + option_without_dashes in double_opts)

bold = ("","")
if (os.name == 'posix'):
    bold = ('\033[0m', '\033[1m')

for arg in sys.argv[1:]:
    if arg == '--coverage':
        ENABLE_COVERAGE = 1
    elif (p.match(arg) or arg in ('-h', '-help')):
        if arg not in private_double_opts:
            if arg == '--help' or arg == '-help' or arg == '-h':
                passOn = '--help'
                showHelp = True
            else:
                if passOn != '--help':
                    passOn += " " + arg
        # add it to double_opts only for validation
        double_opts.add(arg)
    elif p_parallel.match(arg):
        run_parallel = int(arg.split(sep='=', maxsplit=1)[1])

    else:
        # this is for single-dash options only
        # they are interpreted only by this script
        opts.add(arg)

# check for unrecognized options
bad_opts_found = []
bad_opt_str="Unrecognized option: %s"
for o in opts | double_opts:
    if o.startswith('--'):
        if o.split("=")[0] not in framework_opts + test_script_opts + private_double_opts:
            print(bad_opt_str % o)
            bad_opts_found.append(o)
    elif o.startswith('-'):
        if o not in private_single_opts:
            print(bad_opt_str % o)
            bad_opts_found.append(o)
            print("Run with -h to get help on usage.")
            sys.exit(1)

#Set env vars
if "BITCOIND" not in os.environ:
    os.environ["BITCOIND"] = BUILDDIR + '/src/bitcoind' + EXEEXT
if "BITCOINCLI" not in os.environ:
    os.environ["BITCOINCLI"] = BUILDDIR + '/src/bitcoin-cli' + EXEEXT

#Disable Windows tests by default
if EXEEXT == ".exe" and not option_passed('win'):
    print("Win tests currently disabled.  Use -win option to enable")
    sys.exit(0)

if not (ENABLE_WALLET == 1 and ENABLE_UTILS == 1 and ENABLE_BITCOIND == 1):
    print("No rpc tests to run. Wallet, utils, and bitcoind must all be enabled")
    sys.exit(0)

# python3-zmq may not be installed. Handle this gracefully and with some helpful info
if ENABLE_ZMQ:
    try:
        import zmq
    except ImportError as e:
        print("ERROR: \"import zmq\" failed. Set ENABLE_ZMQ=0 or " \
            "to run zmq tests, see dependency info in /qa/README.md.")
        raise e

#Tests
testScripts = [ RpcTest(t) for t in [
    Disabled('sigchecks_inputstandardness_activation', 'Already activated, and mempool bad sigcheck mempool cleanup removed so test will fail'),
    'command_line_args',
    'finalizeblock',
    'txindex',
    Disabled('schnorr-activation', 'Need to be updated to work with BU'),
    'schnorrsig',
    'segwit_recovery',
    'bip135basic',
    'ctor',
    'mining_ctor',
    Disabled('nov152018_forkactivation','Nov 2018 already activated'),
    'blockstorage',
    'miningtest',
    'cashlibtest',
    'tweak',
    'notify',
    Disabled('may152018_forkactivation_1','May 2018 already activated, use it as template to test future upgrade activation'),
    Disabled('may152018_forkactivation_2','May 2018 already activated, use it as template to test future upgrade activation'),
    'validateblocktemplate',
    'parallel',
    'wallet',
    'wallet-hd',
    'wallet-dump',
    'excessive',
    Disabled('uahf', 'temporary disable while waiting, to use as a template for future tests'),
    'listtransactions',
    'receivedby',
    'mempool_resurrect_test',
    'txn_doublespend --mineblock',
    'txn_clone',
    'getchaintips',
    'rawtransactions',
    'rest',
    'mempool_accept',
    'mempool_spendcoinbase',
    'mempool_reorg',
    'mempool_limit',
    'mempool_persist',
    'mempool_validate',
    'mempoolsync',
    'mempool_push',
    'httpbasics',
    'multi_rpc',
    'zapwallettxes',
    'proxy_test',
    'merkle_blocks',
    'fundrawtransaction',
    'signrawtransactions',
    'nodehandling',
    'reindex',
    'decodescript',
    Disabled('p2p-fullblocktest', "TODO"),
    'blockchain',
    'disablewallet',
    'sendheaders',
    'keypool',
    'prioritise_transaction',
    Disabled('invalidblockrequest', "TODO"),
    'invalidtxrequest',
    'abandonconflict',
    Disabled('p2p-versionbits-warning', "Need to resolve issue with false positive warnings on mainnet"),
    'importprunedfunds',
    'compactblocks_1',
    'compactblocks_2',
    'graphene_optimized',
    'graphene_versions',
    'graphene_stage2',
    'thinblocks',
    Disabled('checkdatasig_activation', "CDSV has been already succesfully activated, keep test around as a template for other OP activation"),
    'extversion',
    'sighashmatch',
    'getlogcategories',
    'getrawtransaction',
    'rpc_getblockstats',
    'minimaldata',
    'schnorrmultisig',
    'uptime',
    'op_reversebytes'
] ]

testScriptsExt = [ RpcTest(t) for t in [
    'walletbackup',
    'bip68-112-113-p2p',
    'limits',
    'weirdtx',
    'txPerf',
    'excessive --extensive',
    'parallel --extensive',
    'bip65-cltv',
    'bip68_sequence',
    Disabled('bipdersig-p2p', "keep as an example of testing fork activation"),
    'bipdersig',
    'bip135-grace',
    'bip135-grace-failed',
    'bip135-threshold',
    'getblocktemplate_longpoll',
    'getblocktemplate_proposals',
    'txn_doublespend',
    'txn_clone --mineblock',
    Disabled('pruning', "too much disk"),
    'invalidateblock',
    Disabled('rpcbind_test', "temporary, bug in libevent, see #6655"),
    'smartfees',
    Disabled('maxblocksinflight', "needs a rewrite and is already somewhat tested in sendheaders.py"),
    'p2p-acceptblock',
    'mempool_packages',
    'maxuploadtarget'
] ]

testScriptsElectrum = [ RpcTest(WhenElectrumFound(t)) for t in [
    'electrum_basics',
    'electrum_blockchain_address',
    'electrum_cashaccount',
    'electrum_reorg',
    'electrum_scripthash_gethistory',
    'electrum_server_features',
    'electrum_shutdownonerror',
    'electrum_subscriptions',
    'electrum_transaction_get',
    'electrum_doslimit',
    'electrum_mempool_chain'
] ]

#Enable ZMQ tests
if ENABLE_ZMQ == 1:
    testScripts.append(RpcTest('zmq_test'))
    testScripts.append(RpcTest('interface_zmq'))
    testScripts.append(RpcTest('rpc_zmq'))

def show_wrapper_options():
    """ print command line options specific to wrapper """
    print("Wrapper options:")
    print()
    print("  -extended/--extended  run the extended set of tests")
    print("  -only-extended / -extended-only\n" + \
          "  --only-extended / --extended-only\n" + \
          "  --only-electrum / --electrum-only\n" + \
          "                        run ONLY the extended tests")
    print("  -list / --list        only list test names")
    print("  -win / --win          signal running on Windows and run those tests")
    print("  -f / -force-enable / --force-enable\n" + \
          "                        attempt to run disabled/skipped tests")
    print("  -h / -help / --help   print this help")

def runtests():
    global passOn
    coverage = None
    test_passed = []
    disabled = []
    skipped = []
    tests_to_run = []

    force_enable = option_passed('force-enable') or '-f' in opts
    run_only_extended = option_passed('only-extended') or option_passed('extended-only')
    run_only_electrum = option_passed('only-electrum') or option_passed('electrum-only')
    if run_only_electrum and (run_only_extended or option_passed('extended')):
        raise Exception("electrum only and extended are not compatible options")

    if option_passed('list'):
        if run_only_electrum:
            for t in testScriptsElectrum:
                print(t)
        elif run_only_extended:
            for t in testScriptsExt:
                print(t)
        else:
            for t in testScripts + testScriptsElectrum:
                print(t)
            if option_passed('extended'):
                for t in testScriptsExt:
                    print(t)
        sys.exit(0)

    if ENABLE_COVERAGE:
        coverage = RPCCoverage()
        print("Initializing coverage directory at %s\n" % coverage.dir)

    if(ENABLE_WALLET == 1 and ENABLE_UTILS == 1 and ENABLE_BITCOIND == 1):
        rpcTestDir = RPC_TESTS_DIR
        buildDir   = BUILDDIR
        run_extended = option_passed('extended') or run_only_extended
        cov_flag = coverage.flag if coverage else ''
        flags = " --srcdir %s/src %s %s" % (buildDir, cov_flag, passOn)

        # compile the list of tests to check

        # check for explicit tests
        if showHelp:
            tests_to_run = [ testScripts[0] ]
        else:
            for o in opts:
                if not o.startswith('-'):
                    found = False
                    for t in testScripts + testScriptsElectrum + testScriptsExt:
                        t_rep = str(t).split(' ')
                        if (t_rep[0] == o or t_rep[0] == o + '.py') and len(t_rep) > 1:
                            # it is a test with args - check all args match what was passed, otherwise don't add this test
                            t_args = t_rep[1:]
                            all_args_found = True
                            for targ in t_args:
                                if not targ in passOn.split(' '):
                                    all_args_found = False
                            if all_args_found:
                                tests_to_run.append(t)
                                found = True
                        elif t_rep[0] == o or t_rep[0] == o + '.py':
                            passOnSplit = [x for x in passOn.split(' ') if x != '']
                            found_non_framework_opt = False
                            for p in passOnSplit:
                                if p in test_script_opts:
                                    found_non_framework_opt = True
                            if not found_non_framework_opt:
                                tests_to_run.append(t)
                                found = True
                    if not found:
                        print("Error: %s is not a known test." % o)
                        sys.exit(1)

        # if no explicit tests specified, use the lists
        if not len(tests_to_run):
            if run_only_electrum:
                tests_to_run = testScriptsElectrum
            elif run_only_extended:
                tests_to_run = testScriptsExt
            else:
                tests_to_run += testScripts
                tests_to_run += testScriptsElectrum
                if run_extended:
                    tests_to_run += testScriptsExt

        # weed out the disabled / skipped tests and print them beforehand
        # this allows earlier intervention in case a test is unexpectedly
        # skipped
        if not force_enable:
            trimmed_tests_to_run = []
            for t in tests_to_run:
                if t.is_disabled():
                    print("Disabled testscript %s%s%s (reason: %s)" % (bold[1], t, bold[0], t.reason))
                    disabled.append(str(t))
                elif t.is_skipped():
                    print("Skipping testscript %s%s%s on this platform (reason: %s)" % (bold[1], t, bold[0], t.reason))
                    skipped.append(str(t))
                else:
                    trimmed_tests_to_run.append(t)
            tests_to_run = trimmed_tests_to_run

        # if all specified tests are disabled just quit
        if len(tests_to_run) == 0:
            quit()

        if len(tests_to_run) > 1 and run_parallel:
            # Populate cache
            subprocess.check_output([RPC_TESTS_DIR + 'create_cache.py'] + [flags]+
                                    (["--no-ipv6-rpc-listen"] if option_passed("no-ipv6-rpc-listen") else []))

        tests_to_run = list(map(str,tests_to_run))
        max_len_name = len(max(tests_to_run, key=len))
        time_sum = 0
        time0 = time.time()
        job_queue = RPCTestHandler(run_parallel, tests_to_run, flags)
        results = BOLD[1] + "%s | %s | %s\n\n" % ("TEST".ljust(max_len_name), "PASSED", "DURATION") + BOLD[0]
        all_passed = True

        for _ in range(len(tests_to_run)):
            (name, retCode, coreOutput, stdout, stderr, stderr_filtered, passed, duration) = job_queue.get_next()
            test_passed.append(passed)
            all_passed = all_passed and passed
            time_sum += duration
            results += "%s | %s | %s s\n" % (name.ljust(max_len_name), str(passed).ljust(6), duration)

            print("")
            if inTravis():
                print("travis_fold:start:%s_%s" % (name, passed))
            print(BOLD[1] + name + BOLD[0] + ": Pass: %s%s%s, Duration: %s s" % (BOLD[1], passed, BOLD[0], duration))
            print("#"*50)
            print("- " + retCode)
            if stdout != "":
                print('- stdout '+("-"*50)+"\n", stdout)
            if stderr != "":
                print('- stderr '+("-"*50)+"\n", stderr)
            if coreOutput != "":
                if inTravis():  # if in travis we already folded this
                    print(coreOutput)
                else: # so no need for another header
                    print('- core dump '+("-"*50)+"\n", coreOutput)
            #print('stderr_filtered:\n' if not stderr_filtered == '' else '', repr(stderr_filtered))
            print("#"*25)
            if inTravis():
                print("travis_fold:end:%s_%s" % (name, passed))

        results += BOLD[1] + "\n%s | %s | %s s (accumulated)" % ("ALL".ljust(max_len_name), str(all_passed).ljust(6), time_sum) + BOLD[0]
        print(results)
        print("\nRuntime: %s s" % (int(time.time() - time0)))

        if coverage:
            coverage.report_rpc_coverage()

            print("Cleaning up coverage data")
            coverage.cleanup()

        if not showHelp:
            # show some overall results and aggregates
            print()
            print("%d test(s) passed / %d test(s) failed / %d test(s) executed" % (test_passed.count(True),
                                                                       test_passed.count(False),
                                                                       len(test_passed)))
            print("%d test(s) disabled / %d test(s) skipped due to platform" % (len(disabled), len(skipped)))

        # signal that tests have failed using exit code
        sys.exit(not all_passed)

    else:
        print("No rpc tests to run. Wallet, utils, and bitcoind must all be enabled")

class RPCTestHandler:
    """
    Trigger the testscrips passed in via the list.
    """

    def __init__(self, num_tests_parallel, test_list=None, flags=None):
        assert(num_tests_parallel >= 1)
        self.num_jobs = num_tests_parallel
        self.test_list = test_list
        self.flags = flags
        self.num_running = 0
        # In case there is a graveyard of zombie bitcoinds, we can apply a
        # pseudorandom offset to hopefully jump over them.
        # 3750 is PORT_RANGE/MAX_NODES defined in util, but awkward to import into rpc-test.py
        self.portseed_offset = int(time.time() * 1000) % 3750
        self.jobs = []

    def get_next(self):
        while self.num_running < self.num_jobs and self.test_list:
            # Add tests
            self.num_running += 1
            t = self.test_list.pop(0)
            port_seed = ["--portseed={}".format(len(self.test_list) + self.portseed_offset)]
            log_stdout = tempfile.SpooledTemporaryFile(max_size=2**16, mode="w+")
            log_stderr = tempfile.SpooledTemporaryFile(max_size=2**16, mode="w+")
            got_outputs = [False]
            print("Starting %s" % t)
            self.jobs.append((t,
                              time.time(),
                              subprocess.Popen((RPC_TESTS_DIR + t).split() + self.flags.split() + port_seed,
                                               universal_newlines=True,
                                               stdin=subprocess.DEVNULL,
                                               stdout=subprocess.PIPE,
                                               stderr=subprocess.PIPE,
                                               close_fds=True,
                                               restore_signals=True,
                                               start_new_session=True),
                              log_stdout, log_stderr, got_outputs))
        if not self.jobs:
            raise IndexError('pop from empty list')
        while True:
            # Return first proc that finishes
            time.sleep(.5)
            for j in self.jobs:
                (name, time0, proc, log_stdout, log_stderr, got_outputs) = j
                if ((inGitLabCI() or inTravis()) and int(time.time() - time0) > 20 * 60):
                    # In external CI services, timeout individual tests after 20 minutes (to stop tests hanging and not
                    # providing useful output.
                    proc.send_signal(signal.SIGINT)

                # print("handling " + str(proc))

                def comms(timeout):
                    stdout_data, stderr_data = proc.communicate(timeout=timeout)
                    log_stdout.write(stdout_data)
                    log_stderr.write(stderr_data)

                # Poll for new data on stdout and stderr. This is also necessary as to not block
                # the subprocess when the stdout or stderr pipe is full.
                try:
                    # WARNING: There seems to be a bug in python handling of .join() so that
                    # when you do a .join() with a zero or negative timeout, it will not even try
                    # joining the thread. This is for the handling of the stdout/stderr reader threads
                    # in subprocess.py. A sufficiently positive value (and 0.1s seems to be enough)
                    # seems to make the .join() logic to work, and in turn communicate() not to fail
                    # with a timeout, even though the thread is done reading (which was another cause
                    # of a hang)
                    if not got_outputs[0]:
                        comms(0.1)

                    # .communicate() can only be called once and we have to keep in mind now that
                    # communication happened properly (and the files are closed). It _has_ to be called with a non-None
                    # timeout initially, however, to start the communication threads internal to subprocess.Popen(..)
                    # that are necessary to not block on more output than what fits into the OS' pipe buffer.
                    # Note that end-of-communication does not necessarily indicate a finished subprocess.
                    got_outputs[0] = True
                except subprocess.TimeoutExpired:
                    pass
                except ValueError:
                    # There is a bug in communicate that causes this exception if the child process has closed any pipes but is still running
                    # see: https://bugs.python.org/issue35182
                    pass

                # it won't ever communicate() fully because child didn't close sockets
                try:
                    psproc = psutil.Process(proc.pid)
                    if psproc.status() == psutil.STATUS_ZOMBIE:
                        got_outputs[0] = True
                except AttributeError:
                    pass
                except FileNotFoundError:
                    pass # its ok means process exited cleanly
                except psutil.NoSuchProcess:
                    pass

                if got_outputs[0]:
                    retval = proc.returncode if proc.returncode != None else proc.poll()
                    if retval is None:
                        print("%s: should be impossible, got output from communicate but process is alive" % proc.args[0])
                        dumpLogs = True

                    coreOutput = ""
                    try:
                        if inGitLabCI():
                            coreDir = os.path.join(os.environ.get("CI_PROJECT_DIR", None), "cores")
                        else:
                            coreDir = "/tmp/cores"
                        cores = os.listdir(coreDir)
                        for core in cores:
                            print("Trying to analyze core file: " + str(core))
                            fullCoreFile = os.path.join(coreDir, core)
                            bitcoindBin = os.environ["BITCOIND"]
                            path, fil = os.path.split(bitcoindBin)
                            if os.path.isfile(CORE_ANALYSIS_SCRIPT):
                                popenList = ["gdb", "-core", fullCoreFile, bitcoindBin, "-x", CORE_ANALYSIS_SCRIPT, "-batch"]
                            else:
                                popenList = ["gdb", "-core", fullCoreFile, bitcoindBin, "-ex", "thread apply all bt", "-ex", "set pagination 0", "-batch"]
                            gdb = subprocess.Popen(popenList, universal_newlines=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                            (out, err) = gdb.communicate(None, 60)
                            fold_start = ("\ntravis_fold:start:%s\nCore dump analysis\n" % core) if inTravis() else ""
                            fold_end = ("\ntravis_fold:end:%s\n" % core) if inTravis() else ""
                            coreOutput = fold_start + out + "\n-------\n" + err + fold_end
                            # Now delete this file so we don't dump it repeatedly.  Better would be to move it somewhere for export out of the container.
                            if inGitLabCI():
                                newPath = os.path.join(os.environ.get("CI_PROJECT_DIR", None), "saved-cores")
                                shutil.move(fullCoreFile, os.path.join(newPath, str(core) + "-" + str(name)))
                            else:
                                os.remove(fullCoreFile)
                    except Exception as e:
                        print("Exception trying to show core files in " + coreDir + " :" + str(e))

                    returnCode = "Process %s return code: %d" % (" ".join(proc.args),retval)
                    log_stdout.seek(0), log_stderr.seek(0)
                    stdout = log_stdout.read()
                    stderr = log_stderr.read()
                    passed = stderr == "" and proc.returncode == 0

                    # This is a list of expected messages on stderr. If they appear, they do not
                    # necessarily indicate final failure of a test.

                    # These 2 are due to accidental port conflicts caused by running multiple tests simultaneously.
                    stderr_filtered = stderr.replace("Error: Unable to start HTTP server. See debug log for details.", "")
                    stderr_filtered = stderr.replace("Error: Unable to start RPC services. See debug log for details.", "")

                    stderr_filtered = re.sub(r"Error: Unable to bind to 0.0.0.0:[0-9]+ on this computer\. BCH Unlimited is probably already running\.",
                                             "", stderr_filtered)
                    invalid_index = re.compile(r'.*?\n.*?EXCEPTION.*?\n.*?invalid index for tx.*?\n.*?ProcessMessages.*?\n', re.MULTILINE)
                    stderr_filtered = invalid_index.sub("", stderr_filtered)
                    stderr_filtered = stderr_filtered.replace("Error: Failed to listen on any port. Use -listen=0 if you want this.", "")
                    stderr_filtered = stderr_filtered.replace("Error: Failed to listen on all P2P ports. Failing as requested by -bindallorfail.", "")
                    stderr_filtered = stderr_filtered.replace(" ", "")
                    stderr_filtered = stderr_filtered.replace("\n", "")
                    passed = stderr_filtered == "" and proc.returncode == 0
                    self.num_running -= 1
                    self.jobs.remove(j)
                    return name, returnCode, coreOutput, stdout, stderr, stderr_filtered, passed, int(time.time() - time0)
            print('.', end='', flush=True)

class RPCCoverage(object):
    """
    Coverage reporting utilities for pull-tester.

    Coverage calculation works by having each test script subprocess write
    coverage files into a particular directory. These files contain the RPC
    commands invoked during testing, as well as a complete listing of RPC
    commands per `bitcoin-cli help` (`rpc_interface.txt`).

    After all tests complete, the commands run are combined and diff'd against
    the complete list to calculate uncovered RPC commands.

    See also: qa/rpc-tests/test_framework/coverage.py

    """
    def __init__(self):
        self.dir = tempfile.mkdtemp(prefix="coverage")
        self.flag = '--coveragedir %s' % self.dir

    def report_rpc_coverage(self):
        """
        Print out RPC commands that were unexercised by tests.

        """
        uncovered = self._get_uncovered_rpc_commands()

        if uncovered:
            print("Uncovered RPC commands:")
            print("".join(("  - %s\n" % i) for i in sorted(uncovered)))
        else:
            print("All RPC commands covered.")

    def cleanup(self):
        return shutil.rmtree(self.dir)

    def _get_uncovered_rpc_commands(self):
        """
        Return a set of currently untested RPC commands.

        """
        # This is shared from `qa/rpc-tests/test-framework/coverage.py`
        REFERENCE_FILENAME = 'rpc_interface.txt'
        COVERAGE_FILE_PREFIX = 'coverage.'

        coverage_ref_filename = os.path.join(self.dir, REFERENCE_FILENAME)
        coverage_filenames = set()
        all_cmds = set()
        covered_cmds = set()

        if not os.path.isfile(coverage_ref_filename):
            raise RuntimeError("No coverage reference found")

        with open(coverage_ref_filename, 'r') as f:
            all_cmds.update([i.strip() for i in f.readlines()])

        for root, dirs, files in os.walk(self.dir):
            for filename in files:
                if filename.startswith(COVERAGE_FILE_PREFIX):
                    coverage_filenames.add(os.path.join(root, filename))

        for filename in coverage_filenames:
            with open(filename, 'r') as f:
                covered_cmds.update([i.strip() for i in f.readlines()])

        return all_cmds - covered_cmds


if __name__ == '__main__':
    runtests()
