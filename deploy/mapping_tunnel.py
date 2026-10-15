#!/usr/bin/env python3
"""Keep the read-only point-cloud SSH tunnel aligned with the camera host."""
import argparse
import configparser
from pathlib import Path
import re
import signal
import subprocess
import threading
from urllib.parse import urlsplit


def configured_host(settings_path):
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.read(str(settings_path), encoding="utf-8")
    raw = config.get("VideoStream", "host", fallback="").strip().strip('"')
    host = urlsplit(raw).hostname if raw.startswith("rtsp://") else raw
    if not host or len(host) > 253 or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.-]*", host):
        raise ValueError("waiting for a valid onboard host in VideoStream settings")
    return host


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--settings", type=Path, default=Path.home() / ".config/Agri GCS/无人机地面站.conf")
    parser.add_argument("--identity", type=Path, default=Path.home() / ".ssh/agri_mapping_ed25519")
    parser.add_argument("--user", default="ly")
    parser.add_argument("--port", type=int, default=37262)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or not re.fullmatch(r"[a-z_][a-z0-9_-]*", args.user):
        parser.error("invalid SSH user or mapping port")
    stopping = threading.Event()
    signal.signal(signal.SIGTERM, lambda *_: stopping.set())
    signal.signal(signal.SIGINT, lambda *_: stopping.set())
    last_status = ""
    while not stopping.is_set():
        child = None
        try:
            host = configured_host(args.settings)
            if not args.identity.is_file():
                raise ValueError("mapping SSH identity has not been provisioned")
            command = ["ssh", "-N", "-T", "-i", str(args.identity),
                       "-o", "BatchMode=yes", "-o", "IdentitiesOnly=yes",
                       "-o", "StrictHostKeyChecking=yes", "-o", "ConnectTimeout=5",
                       "-o", "HostKeyAlias=agri-onboard-pointcloud",
                       "-o", "ExitOnForwardFailure=yes", "-o", "ServerAliveInterval=5",
                       "-o", "ServerAliveCountMax=2", "-L",
                       "127.0.0.1:{0}:127.0.0.1:{0}".format(args.port),
                       "{}@{}".format(args.user, host)]
            status = "forwarding 127.0.0.1:{} through {}".format(args.port, host)
            if status != last_status:
                print(status, flush=True)
                last_status = status
            child = subprocess.Popen(command, stdin=subprocess.DEVNULL)
            while not stopping.wait(1) and child.poll() is None:
                if configured_host(args.settings) != host:
                    print("onboard host changed; reconnecting tunnel", flush=True)
                    break
        except (OSError, ValueError, configparser.Error) as exc:
            status = str(exc)
            if status != last_status:
                print(status, flush=True)
                last_status = status
        finally:
            if child is not None and child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
        stopping.wait(2)


if __name__ == "__main__":
    main()
