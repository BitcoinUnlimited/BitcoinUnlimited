# Copyright (c) 2019 The Bitcoin Unlimited developers

from test_framework.loginit import logging

def compare(node, key, expected, is_debug_data = False):
    info = node.getelectruminfo()
    if is_debug_data:
        info = info['debuginfo']
        key = "electrs_" + key
    logging.debug("expecting %s == %s from %s", key, expected, info)
    if key not in info:
        return False
    return info[key] == expected

def bitcoind_electrum_args():
    import random
    return ["-electrum=1", "-debug=electrum", "-debug=rpc",
            "-electrum.port=" + str(random.randint(40000, 60000)),
            "-electrum.monitoring.port=" + str(random.randint(40000, 60000))]
