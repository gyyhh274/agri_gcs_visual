#!/usr/bin/env python3
"""Local nest PLC contract simulator. Uses no hardware; binds to loopback only."""
import argparse
import asyncio
import struct
import time


class Nest:
    def __init__(self, args):
        self.args = args
        self.flags = 0x82 | (0x10 if args.fault else 0) | (0x20 if args.interlock else 0)
        self.fault = 1 if args.fault else 0
        self.ack = 0
        self.last_sequence = 0
        self.last_command = 0
        self.deadline = None
        self.started = time.monotonic()

    def update(self):
        if self.deadline is not None and time.monotonic() >= self.deadline and not self.args.no_complete:
            self.flags = (self.flags & ~0x0f) | (1 if self.last_command == 1 else 2)
            self.deadline = None

    def handle(self, pdu):
        self.update()
        if not pdu:
            return b''
        fc = pdu[0]
        def error(code):
            return bytes((fc | 0x80, code))
        if fc in (3, 4):
            if len(pdu) != 5:
                return error(3)
            address, count = struct.unpack('>HH', pdu[1:])
            if count < 1 or count > 125:
                return error(3)
            if address < 100 or address + count > 104:
                return error(2)
            heartbeat = 0 if self.args.freeze_heartbeat else int(time.monotonic() - self.started) % 65536
            registers = [self.flags, self.fault, self.ack, heartbeat]
            return bytes((fc, count * 2)) + struct.pack('>' + 'H' * count, *registers[address - 100:address - 100 + count])
        if fc == 16:
            if len(pdu) != 10:
                return error(3)
            address, count, byte_count, command, sequence = struct.unpack('>HHBHH', pdu[1:])
            if address != 200:
                return error(2)
            if count != 2 or byte_count != 4 or command not in (1, 2, 3) or sequence == 0:
                return error(3)
            if command != 3 and self.flags & 0x70:
                return error(4)
            if sequence != self.last_sequence:
                self.last_sequence, self.last_command = sequence, command
                if not self.args.no_ack:
                    self.ack = sequence
                if command == 3:
                    self.flags &= ~0x0c
                    self.deadline = None
                else:
                    self.flags = (self.flags & ~0x0f) | (4 if command == 1 else 8)
                    self.deadline = time.monotonic() + 1.5
                print(f'COMMAND code={command} seq={sequence} (simulation only)', flush=True)
            return bytes((fc,)) + pdu[1:5]
        return error(1)


async def serve(args):
    nest = Nest(args)
    async def client(reader, writer):
        try:
            while True:
                header = await reader.readexactly(7)
                tid, protocol, length, unit = struct.unpack('>HHHB', header)
                if protocol != 0 or not 2 <= length <= 254:
                    break
                pdu = await reader.readexactly(length - 1)
                if unit != 1:
                    continue
                reply = nest.handle(pdu)
                frame = struct.pack('>HHHB', tid, 0, len(reply) + 1, unit) + reply
                if args.response_delay:
                    await asyncio.sleep(args.response_delay)
                if args.fragment_response:
                    writer.write(frame[:4]); await writer.drain(); await asyncio.sleep(0.02)
                    writer.write(frame[4:])
                else:
                    writer.write(frame)
                await writer.drain()
        except (asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            writer.close()
            await writer.wait_closed()
    server = await asyncio.start_server(client, '127.0.0.1', args.port)
    print(f'SIMULATOR 127.0.0.1:{server.sockets[0].getsockname()[1]} Unit=1; feedback=100..103, mailbox=200..201 (zero-based)', flush=True)
    async with server:
        await server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=15020)
    parser.add_argument('--fault', action='store_true')
    parser.add_argument('--interlock', action='store_true')
    parser.add_argument('--freeze-heartbeat', action='store_true')
    parser.add_argument('--no-ack', action='store_true')
    parser.add_argument('--no-complete', action='store_true')
    parser.add_argument('--fragment-response', action='store_true')
    parser.add_argument('--response-delay', type=float, default=0)
    args = parser.parse_args()
    if not 0 <= args.port <= 65535 or args.response_delay < 0:
        parser.error('Invalid port or response delay')
    try:
        asyncio.run(serve(args))
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
