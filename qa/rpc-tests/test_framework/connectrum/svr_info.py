#
# Store information about servers. Filter and select based on their protocol support, etc.
#

# Runtime check for optional modules
from importlib import util as importutil

# Check if bottom is present.
if importutil.find_spec("bottom") is not None:
    have_bottom = True
else:
    have_bottom = False

import time, random, json
from .constants import DEFAULT_PORTS


class ServerInfo(dict):
    '''
        Information to be stored on a server. Originally based on IRC data published to a channel.

    '''
    FIELDS = ['nickname', 'hostname', 'ports', 'version', 'pruning_limit' ]

    def __init__(self, nickname_or_dict, hostname=None, ports=None,
                        version=None, pruning_limit=None, ip_addr=None):

        if not hostname and not ports:
            # promote a dict, or similar
            super(ServerInfo, self).__init__(nickname_or_dict)
            return

        self['nickname'] = nickname_or_dict or None
        self['hostname'] = hostname
        self['ip_addr'] = ip_addr or None

        # For 'ports', take
        # - a number (int), assumed to be TCP port, OR
        # - a list of codes, OR
        # - a string to be split apart.
        # Keep version and pruning limit separate
        #
        if isinstance(ports, int):
            ports = ['t%d' % ports]
        elif isinstance(ports, str):
            ports = ports.split()

        # check we don't have junk in the ports list
        for p in ports.copy():
            if p[0] == 'v':
                version = p[1:]
                ports.remove(p)
            elif p[0] == 'p':
                try:
                    pruning_limit = int(p[1:])
                except ValueError:
                    # ignore junk
                    pass
                ports.remove(p)

        assert ports, "Must have at least one port/protocol"

        self['ports'] = ports
        self['version'] = version
        self['pruning_limit'] = int(pruning_limit or 0)

    @classmethod
    def from_response(cls, response_list):
        # Create a list of servers based on data from response from Stratum.
        # Give this the response to: "server.peers.subscribe"
        #
        #     [...
        #      "91.63.237.12",
        #      "electrum3.hachre.de",
        #      [ "v1.0", "p10000", "t", "s" ]
        #       ...]
        #
        rv = []

        for params in response_list:
            ip_addr, hostname, ports = params

            if ip_addr == hostname:
                ip_addr = None

            rv.append(ServerInfo(None, hostname, ports, ip_addr=ip_addr))

        return rv

    @classmethod
    def from_dict(cls, d):
        n = d.pop('nickname', None)
        h = d.pop('hostname')
        p = d.pop('ports')
        rv = cls(n, h, p)
        rv.update(d)
        return rv


    @property
    def protocols(self):
        rv = set(i[0] for i in self['ports'])
        assert 'p' not in rv, 'pruning limit got in there'
        assert 'v' not in rv, 'version got in there'
        return rv

    @property
    def pruning_limit(self):
        return self.get('pruning_limit', 100)

    @property
    def hostname(self):
        return self.get('hostname')

    def get_port(self, for_protocol):
        '''
            Return (hostname, port number, ssl) pair for the protocol.
            Assuming only one port per host.
        '''
        assert len(for_protocol) == 1, "expect single letter code"

        use_ssl = for_protocol in ('s', 'g')

        if 'port' in self: return self['hostname'], int(self['port']), use_ssl

        rv = next(i for i in self['ports'] if i[0] == for_protocol)

        port = None
        if len(rv) >= 2:
            try:
                port = int(rv[1:])
            except:
                pass
        port = port or DEFAULT_PORTS[for_protocol]

        return self['hostname'], port, use_ssl

    @property
    def is_onion(self):
        return self['hostname'].lower().endswith('.onion')

    def select(self, protocol='s', is_onion=None, min_prune=0):
        # predicate function for selection based on features/properties
        return ((protocol in self.protocols)
                    and (self.is_onion == is_onion if is_onion is not None else True)
                    and (self.pruning_limit >= min_prune))

    def __repr__(self):
        return '<ServerInfo {hostname} nick={nickname} ports="{ports}" v={version} prune={pruning_limit}>'\
                                        .format(**self)

    def __str__(self):
        # used as a dict key in a few places.
        return self['hostname'].lower()

    def __hash__(self):
        # this one-line allows use as a set member, which is really handy!
        return hash(self['hostname'].lower())

class KnownServers(dict):
    '''
        Store a list of known servers and their port numbers, etc.

        - can add single entries
        - can read from a CSV for seeding/bootstrap
        - can read from IRC channel to find current hosts

        We are a dictionary, with key being the hostname (in lowercase) of the server.
    '''

    def from_json(self, fname):
        '''
            Read contents of a CSV containing a list of servers.
        '''
        with open(fname, 'rt') as fp:
            for row in json.load(fp):
                nn = ServerInfo.from_dict(row)
                self[str(nn)] = nn

    def from_irc(self, irc_nickname=None, irc_password=None):
        '''
            Connect to the IRC channel and find all servers presently connected.

            Slow; takes 30+ seconds but authoritative and current.

            OBSOLETE.
        '''
        if have_bottom:
            from .findall import IrcListener

            # connect and fetch current set of servers who are
            # on #electrum channel at freenode

            bot = IrcListener(irc_nickname=irc_nickname, irc_password=irc_password)
            results = bot.loop.run_until_complete(bot.collect_data())
            bot.loop.close()

            # merge by nick name
            self.update(results)
        else:
            return(False)

    def add_single(self, hostname, ports, nickname=None, **kws):
        '''
            Explicitly add a single entry.
            Hostname is a FQDN and ports is either a single int (assumed to be TCP port)
            or Electrum protocol/port number specification with spaces in between.
        '''
        nickname = nickname or hostname

        self[hostname.lower()] = ServerInfo(nickname, hostname, ports, **kws)

    def add_peer_response(self, response_list):
        # Update with response from Stratum (lacks the nickname value tho):
        #
        #      "91.63.237.12",
        #      "electrum3.hachre.de",
        #      [ "v1.0", "p10000", "t", "s" ]
        #
        additions = set()
        for params in response_list:
            ip_addr, hostname, ports = params

            if ip_addr == hostname:
                ip_addr = None

            g = self.get(hostname.lower())
            nickname = g['nickname'] if g else None

            here = ServerInfo(nickname, hostname, ports, ip_addr=ip_addr)
            self[str(here)] = here

            if not g:
                additions.add(str(here))

        return additions

    def save_json(self, fname='servers.json'):
        '''
            Write out to a CSV file.
        '''
        rows = sorted(self.keys())
        with open(fname, 'wt') as fp:
            json.dump([self[k] for k in rows], fp, indent=1)

    def dump(self):
        return '\n'.join(repr(i) for i in self.values())

    def select(self, **kws):
        '''
            Find all servers with indicated protocol support. Shuffled.

            Filter by TOR support, and pruning level.
        '''
        lst = [i for i in self.values() if i.select(**kws)]

        random.shuffle(lst)

        return lst


if __name__ == '__main__':

    ks = KnownServers()

    #ks.from_json('servers.json')
    ks.from_irc()

    #print (ks.dump())

    from constants import PROTOCOL_CODES

    print ("%3d: servers in total" % len(ks))

    for tor in [False, True]:
        for pp in PROTOCOL_CODES.keys():
            ll = ks.select(pp, is_onion=tor)
            print ("%3d: %s" % (len(ll), PROTOCOL_CODES[pp] + (' [TOR]' if tor else '')))

# EOF
