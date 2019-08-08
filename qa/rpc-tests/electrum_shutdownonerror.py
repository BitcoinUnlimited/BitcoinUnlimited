#!/usr/bin/env python3
# Copyright (c) 2019 The Bitcoin Unlimited developers
"""
Tests for shutting down Bitcoin Unlimited on electrum server failure
"""
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import waitFor, is_bitcoind_running
import os
import random
import subprocess

# Create a program that exists after 10 seconds.
def create_exiting_program():
    import tempfile

    tmpfh = tempfile.NamedTemporaryFile(suffix = '.c', mode="w", delete=False)
    tmpfh.write("#include <unistd.h>\n")
    tmpfh.write("int main(int argc, char** argv) { sleep(10); return 0; }\n")
    tmpfh.close()

    path_in = tmpfh.name
    path_out = tmpfh.name + ".out"
    try:
        subprocess.check_call(["gcc", "-o", path_out, path_in])
    finally:
        os.unlink(path_in)

    return path_out



class ElectrumShutdownTests(BitcoinTestFramework):

    skip = False
    dummy_electrum_path = None

    def __init__(self):
        super().__init__()
        try:
            self.dummy_electrum_path = create_exiting_program()
        except Exception as e:
            print("SKIPPING TEST - failed to create dummy electrum program: " + str(e))
            self.skip = True

        self.setup_clean_chain = True
        self.num_nodes = 2

        if not self.dummy_electrum_path:
            return

        common_args = ["-electrum=1", "-electrum.exec=%s" % self.dummy_electrum_path]
        self.extra_args = [
            common_args,
            common_args + ["-electrum.shutdownonerror=1"]]

    def run_test(self):
        if self.skip:
            return

        n = self.nodes[0]

        # bitcoind #1 should shutdown when "electrs" does
        waitFor(30, lambda: not is_bitcoind_running(1))

        # bitcoind #0 should not have exited, even though "electrs" has
        assert(is_bitcoind_running(0))

        # del so the test framework doesn't try to stop the stopped node
        del self.nodes[1]

        if self.dummy_electrum_path:
            os.unlink(self.dummy_electrum_path)

    def setup_network(self, dummy = None):
        self.nodes = self.setup_nodes()

if __name__ == '__main__':
    ElectrumShutdownTests().main()

