#!/usr/bin/env python3 
#
#
import bottom, random, time, asyncio
from .svr_info import ServerInfo
import logging

logger = logging.getLogger(__name__)

class IrcListener(bottom.Client):
    def __init__(self, irc_nickname=None, irc_password=None, ssl=True):
        self.my_nick = irc_nickname or 'XC%d' % random.randint(1E11, 1E12)
        self.password = irc_password or None

        self.results = {}       # by hostname
        self.servers = set()
        self.all_done = asyncio.Event()

        super(IrcListener, self).__init__(host='irc.freenode.net', port=6697 if ssl else 6667, ssl=ssl)

        # setup event handling
        self.on('CLIENT_CONNECT', self.connected)
        self.on('PING', self.keepalive)
        self.on('JOIN', self.joined)
        self.on('RPL_NAMREPLY', self.got_users)
        self.on('RPL_WHOREPLY', self.got_who_reply)
        self.on("client_disconnect", self.reconnect)
        self.on('RPL_ENDOFNAMES', self.got_end_of_names)

    async def collect_data(self):
        # start it process
        self.loop.create_task(self.connect())

        # wait until done
        await self.all_done.wait()

        # return the results
        return self.results

    def connected(self, **kwargs):
        logger.debug("Connected")
        self.send('NICK', nick=self.my_nick)
        self.send('USER', user=self.my_nick, realname='Connectrum Client')
        # long delay here as it does an failing Ident probe (10 seconds min)
        self.send('JOIN', channel='#electrum')
        #self.send('WHO', mask='E_*')

    def keepalive(self, message, **kwargs):
        self.send('PONG', message=message)

    async def joined(self, nick=None, **kwargs):
        # happens when we or someone else joins the channel
        # seem to take 10 seconds or longer for me to join
        logger.debug('Joined: %r' % kwargs)

        if nick != self.my_nick:
            await self.add_server(nick)

    async def got_who_reply(self, nick=None, real_name=None, **kws):
        '''
            Server replied to one of our WHO requests, with details.
        '''
        #logger.debug('who reply: %r' % kws)

        nick = nick[2:] if nick[0:2] == 'E_' else nick
        host, ports = real_name.split(' ', 1)

        self.servers.remove(nick)

        logger.debug("Found: '%s' at %s with port list: %s",nick, host, ports)
        self.results[host.lower()] = ServerInfo(nick, host, ports)

        if not self.servers:
            self.all_done.set()

    async def got_users(self, users=[], **kws):
        # After successful join to channel, we are given a list of 
        # users on the channel. Happens a few times for busy channels.
        logger.debug('Got %d (more) users in channel', len(users))

        for nick in users:
            await self.add_server(nick)

    async def add_server(self, nick):
        # ignore everyone but electrum servers
        if nick.startswith('E_'):
            self.servers.add(nick[2:])

    async def who_worker(self):
        # Fetch details on each Electrum server nick we see
        logger.debug('who task starts')
        copy = self.servers.copy()
        for nn in copy:
            logger.debug('do WHO for: ' + nn)
            self.send('WHO', mask='E_'+nn)

        logger.debug('who task done')

    def got_end_of_names(self, *a, **k):
        logger.debug('Got all the user names')

        assert self.servers, "No one on channel!"

        # ask for details on all of those users
        self.loop.create_task(self.who_worker())


    async def reconnect(self, **kwargs):
        # Trigger an event that may cascade to a client_connect.
        # Don't continue until a client_connect occurs, which may be never.

        logger.warn("Disconnected (will reconnect)")

        # Note that we're not in a coroutine, so we don't have access
        # to await and asyncio.sleep
        time.sleep(3)

        # After this line we won't necessarily be connected.
        # We've simply scheduled the connect to happen in the future
        self.loop.create_task(self.connect())

        logger.debug("Reconnect scheduled.")


if __name__ == '__main__':


    import logging
    logging.getLogger('bottom').setLevel(logging.DEBUG)
    logging.getLogger('connectrum').setLevel(logging.DEBUG)
    logging.getLogger('asyncio').setLevel(logging.DEBUG)


    bot = IrcListener(ssl=False)
    bot.loop.set_debug(True)
    fut = bot.collect_data()
    #bot.loop.create_task(bot.connect())
    rv = bot.loop.run_until_complete(fut)

    print(rv)

