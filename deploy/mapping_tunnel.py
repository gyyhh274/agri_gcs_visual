#!/usr/bin/env python3
"""Keep point-cloud and mapping-start SSH tunnels aligned with the camera host."""
import argparse
import os
import configparser
from pathlib import Path
import re
import signal
import subprocess
import time
import threading
from urllib.parse import urlsplit


def configured_user(settings_path):
    """界面上填的「用户」是隧道的唯一身份来源；读不到时回退 --user。"""
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.read(str(settings_path), encoding="utf-8")
    user = config.get("VideoStream", "sshUser", fallback="").strip().strip('"')
    if not re.fullmatch(r"[a-z_][a-z0-9_-]*", user):
        return ""
    return user


def configured_password(settings_path):
    """只有显式选择密码认证时才返回口令；否则返回空串（走密钥）。"""
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.read(str(settings_path), encoding="utf-8")
    mode = config.get("VideoStream", "sshUsePassword", fallback="").strip().strip('"').lower()
    if mode not in ("true", "1", "yes"):
        return ""
    raw = config.get("VideoStream", "sshPassword", fallback="")
    if len(raw) >= 2 and raw.startswith('"') and raw.endswith('"'):
        raw = raw[1:-1]
        out = []
        index = 0
        while index < len(raw):
            if raw[index] == "\\" and index + 1 < len(raw):
                out.append(raw[index + 1])
                index += 2
                continue
            out.append(raw[index])
            index += 1
        return "".join(out)
    return raw


def state_dir():
    path = Path.home() / ".local/state/agri-gcs"
    path.mkdir(parents=True, exist_ok=True)
    try:
        path.chmod(0o700)
    except OSError:
        pass
    return path


def askpass_helper():
    """ssh 的 SSH_ASKPASS 程序：只把口令从 600 文件里吐到 stdout。"""
    path = state_dir() / "ssh-askpass.sh"
    if not path.exists() or not path.stat().st_size:
        path.write_text('#!/bin/sh\ncat "$AGRI_SSH_PASSWORD_FILE"\n', encoding="utf-8")
        path.chmod(0o700)
    return path


def write_password_file(password):
    """口令落盘为 600，避免出现在进程参数里。"""
    path = state_dir() / "ssh-password"
    tmp = state_dir() / "ssh-password.tmp"
    fd = os.open(str(tmp), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as handle:
        handle.write(password)
    os.replace(tmp, path)
    return path


def configured_host(settings_path):
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.read(str(settings_path), encoding="utf-8")
    raw = config.get("VideoStream", "host", fallback="").strip().strip('"')
    host = urlsplit(raw).hostname if raw.startswith("rtsp://") else raw
    if not host or len(host) > 253 or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.-]*", host):
        raise ValueError("waiting for a valid onboard host in VideoStream settings")
    return host


def forward_arguments(port, start_port, task_only=False):
    ports = [port] if task_only else [port, start_port]
    result = []
    for value in ports:
        result += ["-L", "127.0.0.1:{0}:127.0.0.1:{0}".format(value)]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--settings", type=Path, default=Path.home() / ".config/Agri GCS/无人机地面站.conf")
    parser.add_argument("--identity", type=Path, default=Path.home() / ".ssh/agri_mapping_ed25519")
    parser.add_argument("--user", default="ly")
    parser.add_argument("--port", type=int, default=37262)
    parser.add_argument("--start-port", type=int, default=37263)
    parser.add_argument("--task-only", action="store_true", help="Forward only --port using a separate task identity")
    args = parser.parse_args()
    if (not 1 <= args.port <= 65535 or not 1 <= args.start_port <= 65535
            or args.port == args.start_port or not re.fullmatch(r"[a-z_][a-z0-9_-]*", args.user)):
        parser.error("invalid SSH user or mapping port")
    stopping = threading.Event()
    signal.signal(signal.SIGTERM, lambda *_: stopping.set())
    signal.signal(signal.SIGINT, lambda *_: stopping.set())
    last_status = ""
    delay = 2
    while not stopping.is_set():
        child = None
        password = ""
        child_started = time.monotonic()
        try:
            host = configured_host(args.settings)
            login_user = configured_user(args.settings) or args.user
            password = configured_password(args.settings)
            command = ["ssh", "-N", "-T"]
            child_env = None
            if password:
                # 用户名 + 密码：不让 ssh 回落到密钥，也不在无 tty 时卡住
                child_env = dict(os.environ)
                child_env["SSH_ASKPASS"] = str(askpass_helper())
                child_env["SSH_ASKPASS_REQUIRE"] = "force"
                child_env["AGRI_SSH_PASSWORD_FILE"] = str(write_password_file(password))
                command += ["-o", "PreferredAuthentications=password",
                            "-o", "PubkeyAuthentication=no",
                            "-o", "NumberOfPasswordPrompts=1"]
            else:
                if not args.identity.is_file():
                    raise ValueError("mapping SSH identity has not been provisioned")
                command += ["-i", str(args.identity),
                            "-o", "BatchMode=yes", "-o", "IdentitiesOnly=yes"]
            command += ["-o", "StrictHostKeyChecking=yes", "-o", "ConnectTimeout=5",
                        "-o", "HostKeyAlias=agri-onboard-pointcloud",
                        "-o", "ExitOnForwardFailure=yes", "-o", "ServerAliveInterval=5",
                        "-o", "ServerAliveCountMax=2"]
            command += forward_arguments(args.port, args.start_port, args.task_only)
            command += ["{}@{}".format(login_user, host)]
            status = "forwarding 127.0.0.1:{} through {} ({})".format(
                args.port, host, "password" if password else "key")
            if status != last_status:
                print(status, flush=True)
                last_status = status
            child_started = time.monotonic()
            child = subprocess.Popen(command, stdin=subprocess.DEVNULL, env=child_env)
            while not stopping.wait(1) and child.poll() is None:
                if (configured_host(args.settings) != host
                        or (configured_user(args.settings) or args.user) != login_user
                        or configured_password(args.settings) != password):
                    print("onboard address or credentials changed; reconnecting tunnel",
                          flush=True)
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
        # 认证被快速拒绝时逐步退避：密码模式下每 2 秒重试会把机载 sshd
        # 刷成 fail2ban 封禁，救援起来很麻烦。
        fast_failure = child is not None and (time.monotonic() - child_started) < 10
        if fast_failure and password:
            print("auth rejected quickly; waiting {}s before retry "
                  "(check username/password in settings)".format(delay), flush=True)
        # 退避期间一旦配置文件被改（用户改了口令/地址）就立刻重试，
        # 不用等满 60 秒
        try:
            stamp = args.settings.stat().st_mtime
        except OSError:
            stamp = None
        deadline = time.monotonic() + delay
        while not stopping.is_set() and time.monotonic() < deadline:
            stopping.wait(1)
            try:
                if stamp is not None and args.settings.stat().st_mtime != stamp:
                    print("settings changed; retrying immediately", flush=True)
                    break
            except OSError:
                pass
        delay = min(delay * 2, 60) if fast_failure else 2


if __name__ == "__main__":
    main()
