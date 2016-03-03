#!/usr/bin/env python2
# Copyright (c) 2015 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import socket

from test_framework.socks5 import Socks5Configuration, Socks5Command, Socks5Server, AddressType
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import *
'''
Test plan:
- Start bitcoind's with different proxy configurations
- Use addnode to initiate connections
- Verify that proxies are connected to, and the right connection command is given
- Proxy configurations to test on bitcoind side:
    - `-proxy` (proxy everything)
    - `-onion` (proxy just onions)
    - `-proxyrandomize` Circuit randomization
- Proxy configurations to test on proxy side,
    - support no authentication (other proxy)
    - support no authentication + user/pass authentication (Tor)
    - proxy on IPv6

- Create various proxies (as threads)
- Create bitcoinds that connect to them
- Manipulate the bitcoinds using addnode (onetry) an observe effects

addnode connect to IPv4
addnode connect to IPv6
addnode connect to onion
addnode connect to generic DNS name
'''


class ProxyTest(BitcoinTestFramework):
    def __init__(self):
        # Create two proxies on different ports
        # ... one unauthenticated
        self.conf1 = Socks5Configuration()
        self.conf1.addr = ('127.0.0.1', 13000 + (os.getpid() % 1000))
        self.conf1.unauth = True
        self.conf1.auth = False
        # ... one supporting authenticated and unauthenticated (Tor)
        self.conf2 = Socks5Configuration()
        self.conf2.addr = ('127.0.0.1', 14000 + (os.getpid() % 1000))
        self.conf2.unauth = True
        self.conf2.auth = True
        # ... one on IPv6 with similar configuration
        self.conf3 = Socks5Configuration()
        self.conf3.af = socket.AF_INET6
        self.conf3.addr = ('::1', 15000 + (os.getpid() % 1000))
        self.conf3.unauth = True
        self.conf3.auth = True

        self.serv1 = Socks5Server(self.conf1)
        self.serv1.start()
        self.serv2 = Socks5Server(self.conf2)
        self.serv2.start()
        self.serv3 = Socks5Server(self.conf3)
        self.serv3.start()

    def setup_nodes(self):
        # Note: proxies are not used to connect to local nodes
        # this is because the proxy to use is based on CService.GetNetwork(), which return NET_UNROUTABLE for localhost
        return start_nodes(4, self.options.tmpdir, extra_args=[
            ['-listen', '-debug=net', '-debug=proxy', '-proxy=%s:%i' % (self.conf1.addr),'-proxyrandomize=1'], 
            ['-listen', '-debug=net', '-debug=proxy', '-proxy=%s:%i' % (self.conf1.addr),'-onion=%s:%i' % (self.conf2.addr),'-proxyrandomize=0'], 
            ['-listen', '-debug=net', '-debug=proxy', '-proxy=%s:%i' % (self.conf2.addr),'-proxyrandomize=1'], 
            ['-listen', '-debug=net', '-debug=proxy', '-proxy=[%s]:%i' % (self.conf3.addr),'-proxyrandomize=0', '-noonion']
            ])

    def node_test(self, node, proxies, auth, test_onion=True):
        rv = []
        # Test: outgoing IPv4 connection through node
        node.addnode("15.61.23.23:1234", "onetry")
        cmd = proxies[0].queue.get()
        assert(isinstance(cmd, Socks5Command))
        # Note: bitcoind's SOCKS5 implementation only sends atyp DOMAINNAME, even if connecting directly to IPv4/IPv6
        assert_equal(cmd.atyp, AddressType.DOMAINNAME)
        assert_equal(cmd.addr, "15.61.23.23")
        assert_equal(cmd.port, 1234)
        if not auth:
            assert_equal(cmd.username, None)
            assert_equal(cmd.password, None)
        rv.append(cmd)

        # Test: outgoing IPv6 connection through node
        node.addnode("[1233:3432:2434:2343:3234:2345:6546:4534]:5443", "onetry")
        cmd = proxies[1].queue.get()
        assert(isinstance(cmd, Socks5Command))
        # Note: bitcoind's SOCKS5 implementation only sends atyp DOMAINNAME, even if connecting directly to IPv4/IPv6
        assert_equal(cmd.atyp, AddressType.DOMAINNAME)
        assert_equal(cmd.addr, "1233:3432:2434:2343:3234:2345:6546:4534")
        assert_equal(cmd.port, 5443)
        if not auth:
            assert_equal(cmd.username, None)
            assert_equal(cmd.password, None)
        rv.append(cmd)

        if test_onion:
            # Test: outgoing onion connection through node
            node.addnode("bitcoinostk4e4re.onion:8333", "onetry")
            cmd = proxies[2].queue.get()
            assert(isinstance(cmd, Socks5Command))
            assert_equal(cmd.atyp, AddressType.DOMAINNAME)
            assert_equal(cmd.addr, "bitcoinostk4e4re.onion")
            assert_equal(cmd.port, 8333)
            if not auth:
                assert_equal(cmd.username, None)
                assert_equal(cmd.password, None)
            rv.append(cmd)

        # Test: outgoing DNS name connection through node
        node.addnode("node.noumenon:8333", "onetry")
        cmd = proxies[3].queue.get()
        assert(isinstance(cmd, Socks5Command))
        assert_equal(cmd.atyp, AddressType.DOMAINNAME)
        assert_equal(cmd.addr, "node.noumenon")
        assert_equal(cmd.port, 8333)
        if not auth:
            assert_equal(cmd.username, None)
            assert_equal(cmd.password, None)
        rv.append(cmd)

        return rv

    def run_test(self):
        # basic -proxy
        self.node_test(self.nodes[0], [self.serv1, self.serv1, self.serv1, self.serv1], False)

        # -proxy plus -onion
        self.node_test(self.nodes[1], [self.serv1, self.serv1, self.serv2, self.serv1], False)

        # -proxy plus -onion, -proxyrandomize
        rv = self.node_test(self.nodes[2], [self.serv2, self.serv2, self.serv2, self.serv2], True)
        # Check that credentials as used for -proxyrandomize connections are unique
        credentials = set((x.username,x.password) for x in rv)
        assert_equal(len(credentials), 4)

        # proxy on IPv6 localhost
        self.node_test(self.nodes[3], [self.serv3, self.serv3, self.serv3, self.serv3], False, False)

        def networks_dict(d):
            r = {}
            for x in d['networks']:
                r[x['name']] = x
            return r

        # test RPC getnetworkinfo
        n0 = networks_dict(self.nodes[0].getnetworkinfo())
        for net in ['ipv4','ipv6','onion']:
            assert_equal(n0[net]['proxy'], '%s:%i' % (self.conf1.addr))
            assert_equal(n0[net]['proxy_randomize_credentials'], True)
        assert_equal(n0['onion']['reachable'], True)

        n1 = networks_dict(self.nodes[1].getnetworkinfo())
        for net in ['ipv4','ipv6']:
            assert_equal(n1[net]['proxy'], '%s:%i' % (self.conf1.addr))
            assert_equal(n1[net]['proxy_randomize_credentials'], False)
        assert_equal(n1['onion']['proxy'], '%s:%i' % (self.conf2.addr))
        assert_equal(n1['onion']['proxy_randomize_credentials'], False)
        assert_equal(n1['onion']['reachable'], True)
        
        n2 = networks_dict(self.nodes[2].getnetworkinfo())
        for net in ['ipv4','ipv6','onion']:
            assert_equal(n2[net]['proxy'], '%s:%i' % (self.conf2.addr))
            assert_equal(n2[net]['proxy_randomize_credentials'], True)
        assert_equal(n2['onion']['reachable'], True)

        n3 = networks_dict(self.nodes[3].getnetworkinfo())
        for net in ['ipv4','ipv6']:
            assert_equal(n3[net]['proxy'], '[%s]:%i' % (self.conf3.addr))
            assert_equal(n3[net]['proxy_randomize_credentials'], False)
        assert_equal(n3['onion']['reachable'], False)

if __name__ == '__main__':
    ProxyTest().main()

