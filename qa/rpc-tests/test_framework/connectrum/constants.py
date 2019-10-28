
# copied values from electrum source

# IDK, maybe?
ELECTRUM_VERSION = '2.6.4'  # version of the client package
PROTOCOL_VERSION = '0.10'   # protocol version requested

# note: 'v' and 'p' are effectively reserved as well.
PROTOCOL_CODES = dict(t='TCP (plaintext)', h='HTTP (plaintext)', s='SSL', g='Websocket')

# from electrum/lib/network.py at Jun/2016
#
DEFAULT_PORTS = { 't':50001, 's':50002, 'h':8081, 'g':8082}

BOOTSTRAP_SERVERS = {
    'erbium1.sytes.net': {'t':50001, 's':50002},
    'ecdsa.net': {'t':50001, 's':110},
    'electrum0.electricnewyear.net': {'t':50001, 's':50002},
    'VPS.hsmiths.com': {'t':50001, 's':50002},
    'ELECTRUM.jdubya.info': {'t':50001, 's':50002},
    'electrum.no-ip.org': {'t':50001, 's':50002, 'g':443},
    'us.electrum.be': DEFAULT_PORTS,
    'bitcoins.sk': {'t':50001, 's':50002},
    'electrum.petrkr.net': {'t':50001, 's':50002},
    'electrum.dragonzone.net': DEFAULT_PORTS,
    'Electrum.hsmiths.com': {'t':8080, 's':995},
    'electrum3.hachre.de': {'t':50001, 's':50002},
    'elec.luggs.co': {'t':80, 's':443},
    'btc.smsys.me': {'t':110, 's':995},
    'electrum.online': {'t':50001, 's':50002},
}



