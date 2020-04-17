#
# Implement an asyncio.Protocol for Electrum (clients)
#
#
import asyncio, json
import logging

logger = logging.getLogger(__name__)

class StratumProtocol(asyncio.Protocol):
    client = None
    closed = False
    transport = None
    buf = b""

    def connection_made(self, transport):
        self.transport = transport
        logger.debug("Transport connected ok")

    def connection_lost(self, exc):
        if not self.closed:
            self.closed = True
            self.close()
            self.client._connection_lost(self)

    def data_received(self, data):
        self.buf += data

        # Unframe the mesage. Expecting JSON.
        *lines, self.buf = self.buf.split(b'\n')

        for line in lines:
            if not line: continue

            try:
                msg = line.decode('utf-8', "error").strip()
            except UnicodeError as exc:
                logger.exception("Encoding issue on %r" % line)
                self.connection_lost(exc)
                return

            try:
                msg = json.loads(msg)
            except ValueError as exc:
                logger.exception("Bad JSON received from server: %r" % msg)
                self.connection_lost(exc)
                return

            #logger.debug("RX:\n%s", json.dumps(msg, indent=2))

            try:
                self.client._got_response(msg)
            except Exception as e:
                logger.exception("Trouble handling response! (%s)" % e)
                continue

    def send_data(self, message):
        '''
            Given an object, encode as JSON and transmit to the server.
        '''
        #logger.debug("TX:\n%s", json.dumps(message, indent=2))
        data = json.dumps(message).encode('utf-8') + b'\n'
        self.transport.write(data)

    def close(self):
        if not self.closed:
            try:
                self.transport.close()
            finally:
                self.closed = True

# EOF
