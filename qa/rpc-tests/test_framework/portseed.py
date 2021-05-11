import logging
import random
import re

# The maximum number of nodes a single test can spawn
MAX_NODES = 8
# Don't assign rpc or p2p ports lower than this
PORT_MIN = 5000
# The number of ports to "reserve" for p2p and rpc, each
PORT_RANGE = 5000
debug_port_assignments = False

class PortSeed:
    # Must be initialized with a unique integer for each process
    n = None

    # These map <node-n> to the ports assigned to it.
    ports = {
            "p2p": {},
            "rpc": {},
            "electrum_rpc": {},
            "electrum_ws": {},
            "electrum_monitoring": {}
    }

    # map node number to full initialized config file path for later fixup in case
    # the RPC or P2P port needs to be changed due to port collisions.
    config_file = {}

    def all_used_ports(self):
        """
        Get a list of all ports we've assigned to nodes.
        """
        used = []
        for service in self.ports.values():
            used.extend(service.values())
        return used

    def ports_as_string(self, n):
        """
        String representation of ports used by a single node.
        """
        ports_string = ""
        for service in self.ports.keys():
            ports_string += "{}: {} ".format(service, self.ports[service][n])

        return ports_string

    def random_port(self):
        """
        Find a random port that we've not already assigned to a node.
        """
        p = random.randint(PORT_MIN, PORT_MIN+PORT_RANGE - 1)
        if p in self.all_used_ports():
            return self.random_port()
        return p


portseed = PortSeed()

def remap_ports(n):
    """
    Re-map all ports assigned to a node. Used in case a node fails to start-up
    (maybe) due to a port conflict.
    """
    for service in portseed.ports.keys():
        portseed.ports[service][n] = portseed.random_port()

    logging.warn("Remapping ports, new ports: {}".format(portseed.ports_as_string(n)))
    fixup_ports_in_configfile(n)

def fixup_ports_in_configfile(i):
    """
    Re-write a nodes config file with currently assigned ports.
    """
    assert(i in PortSeed.config_file)

    logging.warn(
            "Tweaking ports in configuration file {} for node {}".format(
            PortSeed.config_file[i], i))

    cfg_data = open(PortSeed.config_file[i], "r", encoding="utf-8").read()

    cfg_data = re.sub(r"^port=[0-9]+", r"port=%d" % p2p_port(i),
                      cfg_data, flags=re.MULTILINE)
    cfg_data = re.sub(r"^rpcport=[0-9]+", r"rpcport=%d" % rpc_port(i),
                      cfg_data, flags=re.MULTILINE)
    cfg_data = re.sub(r"^electrum\.port=[0-9]+",
            r"electrum.port=%d" % electrum_rpc_port(i),
            cfg_data, flags=re.MULTILINE)
    cfg_data = re.sub(
            r"^electrum\.ws\.port=[0-9]+",
            r"electrum.ws.port=%d" % electrum_ws_port(i),
                      cfg_data, flags=re.MULTILINE)
    cfg_data = re.sub(
            r"^electrum\.monitoring\.port=[0-9]+",
            r"electrum.monitoring.port=%d" % electrum_monitoring_port(i),
            cfg_data, flags=re.MULTILINE)

    with open(PortSeed.config_file[i], "w", encoding="utf-8") as outf:
        outf.write(cfg_data)

def get_port(n, service):
    assert(n <= MAX_NODES)
    assert service in portseed.ports

    if n in portseed.ports[service]:
        return portseed.ports[service][n]

    seed_offset = (MAX_NODES * PortSeed.n) % (PORT_RANGE - 1 - MAX_NODES)

    service_index = list(portseed.ports).index(service)
    port = PORT_MIN + (service_index * PORT_RANGE) + n + seed_offset

    if debug_port_assignments:
        logging.info("Current {} port for node {}: {}".format(service, n, port))

    portseed.ports[service][n] = port
    return port

def p2p_port(n):
    return get_port(n, "p2p")

def rpc_port(n):
    return get_port(n, "rpc")

def electrum_rpc_port(n):
    return get_port(n, "electrum_rpc")

def electrum_ws_port(n):
    return get_port(n, "electrum_ws")

def electrum_monitoring_port(n):
    return get_port(n, "electrum_monitoring")
