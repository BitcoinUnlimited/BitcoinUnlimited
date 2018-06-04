TOR SUPPORT IN BITCOIN
======================

It is possible to run Bitcoin as a Tor hidden service, and connect to such services.

The following directions assume you have a Tor proxy running on port 9050. Many distributions default to having a SOCKS proxy listening on port 9050, but others may not. In particular, the Tor Browser Bundle defaults to listening on a random port. See [Tor Project FAQ:TBBSocksPort](https://www.torproject.org/docs/faq.html.en#TBBSocksPort) for how to properly
configure Tor.


1. Run bitcoin behind a Tor proxy
---------------------------------

The first step is running Bitcoin behind a Tor proxy. This will already make all
outgoing connections be anonymized, but more is possible.

	-proxy=ip:port  Set the proxy server. If SOCKS5 is selected (default), this proxy
	                server will be used to try to reach .onion addresses as well.

	-onion=ip:port  Set the proxy server to use for tor hidden services. You do not
	                need to set this if it's the same as -proxy. You can use -noonion
	                to explicitly disable access to hidden service.

	-listen         When using -proxy, listening is disabled by default. If you want
	                to run a hidden service (see next section), you'll need to enable
	                it explicitly.

	-connect=X      When behind a Tor proxy, you can specify .onion addresses instead
	-addnode=X      of IP addresses or hostnames in these parameters. It requires
	-seednode=X     SOCKS5. In Tor mode, such addresses can also be exchanged with
	                other P2P nodes.

In a typical situation, this suffices to run behind a Tor proxy:

	./bitcoin -proxy=127.0.0.1:9050


2. Run a bitcoin hidden server
------------------------------

If you configure your Tor system accordingly, it is possible to make your node also
reachable from the Tor network. Add these lines to your /etc/tor/torrc (or equivalent
config file):

	HiddenServiceDir /var/lib/tor/bitcoin-service/
	HiddenServicePort 8333 127.0.0.1:8333
	HiddenServicePort 18333 127.0.0.1:18333

The directory can be different of course, but (both) port numbers should be equal to
your bitcoind's P2P listen port (8333 by default).

	-externalip=X   You can tell bitcoin about its publicly reachable address using
	                this option, and this can be a .onion address. Given the above
	                configuration, you can find your onion address in
	                /var/lib/tor/bitcoin-service/hostname. Onion addresses are given
	                preference for your node to advertise itself with, for connections
	                coming from unroutable addresses (such as 127.0.0.1, where the
	                Tor proxy typically runs).

	-listen         You'll need to enable listening for incoming connections, as this
	                is off by default behind a proxy.

	-discover       When -externalip is specified, no attempt is made to discover local
	                IPv4 or IPv6 addresses. If you want to run a dual stack, reachable
	                from both Tor and IPv4 (or IPv6), you'll need to either pass your
	                other addresses using -externalip, or explicitly enable -discover.
	                Note that both addresses of a dual-stack system may be easily
	                linkable using traffic analysis.

In a typical situation, where you're only reachable via Tor, this should suffice:

	./bitcoind -proxy=127.0.0.1:9050 -externalip=57qr3yd1nyntf5k.onion -listen

(obviously, replace the Onion address with your own). It should be noted that you still
listen on all devices and another node could establish a clearnet connection, when knowing
your address. To mitigate this, additionally bind the address of your Tor proxy:

	./bitcoind ... -bind=127.0.0.1

If you don't care too much about hiding your node, and want to be reachable on IPv4
as well, use `discover` instead:

	./bitcoind ... -discover

and open port 8333 on your firewall (or use -upnp).

If you only want to use Tor to reach onion addresses, but not use it as a proxy
for normal IPv4/IPv6 communication, use:

	./bitcoin -onion=127.0.0.1:9050 -externalip=57qr3yd1nyntf5k.onion -discover

3. Automatically listen on Tor
--------------------------------

Starting with Tor version 0.2.7.1 it is possible, through Tor's control socket
API, to create and destroy 'ephemeral' hidden services programmatically.
Bitcoin Unlimited has been updated to make use of this.

This means that if Tor is running (and proper authorization is available),
Bitcoin Unlimited automatically creates a hidden service to listen on, without
manual configuration. This will positively affect the number of available
.onion nodes.

This new feature is enabled by default if Bitcoin Unlimited is listening, and
a connection to Tor can be made. It can be configured with the `-listenonion`,
`-torcontrol` and `-torpassword` settings. To show verbose debugging
information, pass `-debug=tor`.

In more practical way this what you need to do to actually setting tor to operate
in this new configuration. Firstly you need to be sure that the user under which
bitcoind is going to be executed has write permission on tor system directories
(e.g. /var/run/tor/control.authcookie). On Debian and Ubuntu system add such user
to the `debian-tor` group should be enough (i.e. `sudo adduser $USER debian-tor`)

Then add these lines to `/etc/tor/torrc` (i.e `sudo nano /etc/tor/torrc`)

	ControlPort 9051
	CookieAuthentication 1
	HashedControlPassword <TheHashOfYourTorPassword>

to get the hash of your Tor password just use this command

	tor --hash-password <YourTorPassword>

Next you have to restart the Tor service:

	sudo service tor restart

Add these lines to your `bitcoin.conf` file

	proxy=127.0.0.1:9050
	listen=1
	onlynet=onion
	listenonion=1
	discover=0
	torcontrol=127.0.0.1:9051
	torpassword=<TheHashOfYourTorPassword>

Then issue this command to get the url of your onion hidden service

	bitcoin-cli getnetworkinfo | grep -w addr

you should get an output like this one

	"address": "k3a23xgpg2jugxjr.onion"

Pick the onion domain and verify on bitnodes.21.co if you it is
reachable.

If you want to leverage the nature of tor and stop a DDoS attack to your
node, firstly stop your node:

	bitcoin-cli stop

Remove your peer file and tor private key from bitcoin data directory

	cd ~/.bitcoin
	rm onion_private_key
	rm peers.dat

Removing the `onion_private_key` serves the aim of having a new onion URL for
your bitcoin node, in such a way your attacker won't be able to harm you
again in the near term cause the prev URL is not valid any more.

Removing `peer.dat` will let you fetch a bunch of new peers from the seeder
this somewhat reduce the risk of peering again with your attacker.

If you want to maintain the same onion URL across reboot avoid to delete
the onion private key file.

Restart your node:

	bitcoind -daemon
