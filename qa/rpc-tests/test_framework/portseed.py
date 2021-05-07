import logging
import random
import re

# The maximum number of nodes a single test can spawn
MAX_NODES = 8
# Don't assign rpc or p2p ports lower than this
PORT_MIN = 5000
# The number of ports to "reserve" for p2p and rpc, each
PORT_RANGE = 30000
debug_port_assignments = False

class PortSeed:
    # Must be initialized with a unique integer for each process
    n = None

    # these map <node-n> to newly assigned port in case any
    # errors happened during startup.
    port_changes_p2p = {}
    port_changes_rpc = {}

    # map node number to full initialized config file path for later fixup in case
    # the RPC or P2P port needs to be changed due to port collisions.
    config_file = {}

def remap_ports(n):
    new_port_rpc = random.randint(PORT_MIN, PORT_MIN+PORT_RANGE - 1)
    new_port_p2p = random.randint(PORT_MIN, PORT_MIN+PORT_RANGE - 1)

    logging.warn("Remapping RPC for node %d to new random port %d", n, new_port_rpc)
    logging.warn("Remapping P2P for node %d to new random port %d", n, new_port_p2p)
    PortSeed.port_changes_rpc[n] = new_port_rpc
    PortSeed.port_changes_p2p[n] = new_port_p2p

def fixup_ports_in_configfile(i):
    assert(i in PortSeed.config_file)

    logging.warn("Tweaking ports in configuration file %s for node %d", PortSeed.config_file[i], i)
    cfg_data = open(PortSeed.config_file[i], "r", encoding="utf-8").read()

    cfg_data = re.sub(r"^port=[0-9]+", r"port=%d" % p2p_port(i),
                      cfg_data, flags=re.MULTILINE)
    cfg_data = re.sub(r"^rpcport=[0-9]+", r"rpcport=%d" % rpc_port(i),
                      cfg_data, flags=re.MULTILINE)

    with open(PortSeed.config_file[i], "w", encoding="utf-8") as outf:
        outf.write(cfg_data)


def p2p_port(n):
    assert(n <= MAX_NODES)
    if n in PortSeed.port_changes_p2p:
        result = PortSeed.port_changes_p2p[n]
    else:
        result = PORT_MIN + n + (MAX_NODES * PortSeed.n) % (PORT_RANGE - 1 - MAX_NODES)
    if debug_port_assignments:
        logging.info("Current P2P port for node %d: %d", n, result)
    return result

def rpc_port(n):
    assert(n <= MAX_NODES)
    if n in PortSeed.port_changes_rpc:
        result = PortSeed.port_changes_rpc[n]
    else:
        result = PORT_MIN + PORT_RANGE + n + (MAX_NODES * PortSeed.n) % (PORT_RANGE - 1 - MAX_NODES)
    if debug_port_assignments:
        logging.info("Current RPC port for node %d: %d", n, result)
    return result
