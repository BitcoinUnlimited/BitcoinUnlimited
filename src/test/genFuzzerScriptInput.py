#!/usr/bin/python3
# Copyright (c) 2018-2019 The Bitcoin Unlimited developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# This program generates fuzzing input scripts by converting the BCHScript programs specified
# in fuzzScriptStarter.bch to binary input files needed for test_bitcoin_fuzzy.cpp and AFL.

import os
import re
import sys
import argparse
import io
import struct

# to run this program, create a symlink in this directory to the bchscript compiler
import bchscript

modDir = os.path.dirname(os.path.realpath(__file__))

def ser_string(s):
    if len(s) < 253:
        return struct.pack("B", len(s)) + s
    elif len(s) < 0x10000:
        return struct.pack("<BH", 253, len(s)) + s
    elif len(s) < 0x100000000:
        return struct.pack("<BI", 254, len(s)) + s
    return struct.pack("<BQ", 255, len(s)) + s


def Test():
    try:
        os.mkdir("scriptFuzzInputs")
    except OSError:
        pass # already exists

    inp = open(modDir + os.sep + "fuzzScriptStarter.bch", "r")
    ret = bchscript.compile(inp)

    inp = bchscript.script2bin(ret["inp"]["script"])
    del ret["inp"]
    for (k, v) in ret.items():
        fname = "scriptFuzzInputs/aflinput%s.bin" % k
        print("Creating %s" % fname)
        out = bchscript.script2bin(v["script"])
        f = open(fname,"wb")
        flags = 0xd47df # standard + opcode enabling flags
        version = 1  # protocol version -- unused in this
        result = struct.pack("<I",version)
        f.write(result)
        result = struct.pack("<I",flags)
        f.write(result)
        f.write(ser_string(inp))
        f.write(ser_string(out))
        f.close()


if __name__== "__main__":
    Test()
