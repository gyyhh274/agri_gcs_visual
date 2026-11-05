#!/usr/bin/env python3
"""Dependency-free, read-only Modbus TCP probe. No address scanning or writes."""
import argparse
import json
import socket
import struct
from datetime import datetime


def receive_exact(sock, count):
    data = bytearray()
    while len(data) < count:
        part = sock.recv(count - len(data))
        if not part:
            raise ConnectionError('PLC closed the connection before the response completed')
        data.extend(part)
    return bytes(data)


def probe(host, port, unit, function=None, address=None, count=1, timeout=2):
    if not 0 <= unit <= 255 or not 1 <= port <= 65535:
        raise ValueError('Invalid port or Unit ID')
    if function is None:
        function, payload = 43, bytes.fromhex('0e0100')
    else:
        if function not in (1, 2, 3, 4) or address is None or not 0 <= address <= 65535 or not 1 <= count <= (2000 if function <= 2 else 125) or address + count > 65536:
            raise ValueError('Invalid read function, zero-based address or count')
        payload = struct.pack('>HH', address, count)
    request = struct.pack('>HHHBB', 1, 0, len(payload) + 2, unit, function) + payload
    result = {'time': datetime.now().astimezone().isoformat(), 'target': f'{host}:{port}', 'unit': unit,
              'tx': request.hex(' '), 'tcp_connected': False, 'modbus_response_valid': False, 'register_read_ok': False}
    with socket.create_connection((host, port), timeout) as sock:
        sock.settimeout(timeout)
        result['tcp_connected'] = True
        result['local_address'] = sock.getsockname()[0]
        sock.sendall(request)
        header = receive_exact(sock, 7)
        transaction, protocol, length, response_unit = struct.unpack('>HHHB', header)
        if transaction != 1 or protocol != 0 or response_unit != unit or not 2 <= length <= 254:
            raise ValueError('Invalid MBAP header: ' + header.hex(' '))
        pdu = receive_exact(sock, length - 1)
        result['rx'] = (header + pdu).hex(' ')
        if pdu[0] == function | 0x80 and len(pdu) == 2 and pdu[1] != 0:
            result.update(modbus_response_valid=True, exception_code=pdu[1])
        elif pdu[0] == function and function in (1, 2, 3, 4):
            expected = (count + 7) // 8 if function <= 2 else count * 2
            if len(pdu) != expected + 2 or pdu[1] != expected:
                raise ValueError('Invalid read response byte count')
            values = [bool(pdu[2 + i // 8] & (1 << (i % 8))) for i in range(count)] if function <= 2 else list(struct.unpack('>' + 'H' * count, pdu[2:]))
            result.update(modbus_response_valid=True, register_read_ok=True, values=values)
        elif pdu[0] == 43 and len(pdu) >= 7 and pdu[1:3] == bytes.fromhex('0e01'):
            if pdu[3] & 0x7f not in (1, 2, 3) or pdu[4] not in (0, 255):
                raise ValueError('Invalid device identification response')
            offset, objects = 7, {}
            for _ in range(pdu[6]):
                if offset + 2 > len(pdu):
                    raise ValueError('Truncated device identification object')
                key, size = pdu[offset:offset + 2]
                offset += 2
                if offset + size > len(pdu):
                    raise ValueError('Truncated device identification value')
                objects[key] = pdu[offset:offset + size].decode('utf-8', errors='replace')
                offset += size
            if offset != len(pdu):
                raise ValueError('Unexpected device identification data')
            result.update(modbus_response_valid=True, device_objects=objects)
        else:
            raise ValueError('Unexpected function or malformed response')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', default='192.168.2.8')
    parser.add_argument('--port', type=int, default=502)
    parser.add_argument('--unit', type=int, default=1, help='Trial default 1; confirm in PLC project')
    parser.add_argument('--function', type=int, choices=(1, 2, 3, 4))
    parser.add_argument('--address', type=int, help='Confirmed safe decimal PDU address, zero-based')
    parser.add_argument('--count', type=int, default=1)
    args = parser.parse_args()
    if args.function is None and args.address is not None or args.function is not None and args.address is None:
        parser.error('--function and --address must be provided together; omit both for device identification')
    try:
        result = probe(args.host, args.port, args.unit, args.function, args.address, args.count)
    except (OSError, ValueError) as error:
        print(json.dumps({'error': str(error)}, ensure_ascii=False, indent=2))
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result['modbus_response_valid'] and (args.function is None or result['register_read_ok']) else 2


if __name__ == '__main__':
    raise SystemExit(main())
