# (C)opyright 2018 The Bitcoin Unlimited developers
#
# Import this first to initialize logging in a test case This, among
# other things, redirects all logging to stdout.  This is important as
# the rpc-tests.py driver program assumes that anything printed on
# stderr is to be considered a test failure.
import sys
import logging
logging.basicConfig(format='%(asctime)s.%(levelname)s: %(message)s',
                    level=logging.INFO,
                    stream=sys.stdout)
