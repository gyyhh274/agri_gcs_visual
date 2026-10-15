#!/usr/bin/env python3
"""Read-only mapping link diagnostic; no ROS or camera commands are sent."""
import argparse
import json
import socket
import struct
import time


def receive_exact(connection, size):
    result = bytearray()
    while len(result) < size:
        block = connection.recv(size - len(result))
        if not block:
            raise ConnectionError("mapping service closed the stream")
        result.extend(block)
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=37262)
    parser.add_argument("--seconds", type=float, default=5)
    parser.add_argument("--require-cloud", action="store_true")
    args = parser.parse_args()
    deadline = time.monotonic() + args.seconds
    counts = {"cloud": 0, "pose": 0, "status": 0}
    points = 0
    try:
        with socket.create_connection((args.host, args.port), timeout=5) as connection:
            connection.settimeout(3)
            while time.monotonic() < deadline:
                try:
                    magic, header_bytes, payload_bytes = struct.unpack("<4sII", receive_exact(connection, 12))
                except socket.timeout:
                    break
                if magic != b"AGM1" or not 0 < header_bytes <= 16384 or payload_bytes > 8 * 1024 * 1024:
                    raise ValueError("invalid mapping frame limits")
                header = json.loads(receive_exact(connection, header_bytes).decode("utf-8"))
                receive_exact(connection, payload_bytes)
                kind = header.get("type")
                if kind in counts:
                    counts[kind] += 1
                if kind == "cloud":
                    count = header.get("point_count", 0)
                    if type(count) is not int or count < 0 or count * 16 != payload_bytes:
                        raise ValueError("cloud size mismatch")
                    points += count
                if kind != "cloud" or counts["cloud"] == 1:
                    print(json.dumps(header, ensure_ascii=False), flush=True)
        print(json.dumps({"frames": counts, "received_points": points}, ensure_ascii=False))
        return 2 if args.require_cloud and points == 0 else 0
    except (OSError, ValueError, ConnectionError) as exc:
        print("Mapping diagnostic failed: {}".format(exc))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
