#!/usr/bin/env python3
"""在地面站侧启动机载"任务适配器"（用于任务初始化时它自己还没起来的场景）。

安全模型：机载侧为这把密钥配置了 forced command，只允许动作 `start-adapter`，
因此即使密钥泄露也无法执行任意命令。凭据（地址/用户/密码）与隧道脚本一致。

退出码：0 已启动或本就在运行；2 配置问题；4 启动失败；124 超时。
"""
import argparse
import os
import subprocess
import sys
from configparser import ConfigParser
from pathlib import Path

ACTION = "start-adapter"


def state_dir():
    base = os.environ.get("AGRI_GCS_STATE_DIR")
    return Path(base) if base else Path.home() / ".local/state/agri-gcs"


def load_config(path):
    config = ConfigParser()
    config.read(path, encoding="utf-8")
    return config


def setting(config, key, fallback=""):
    return config.get("VideoStream", key, fallback=fallback).strip().strip('"')


def password_of(config):
    if setting(config, "sshUsePassword", "false").lower() != "true":
        return ""
    raw = config.get("VideoStream", "sshPassword", fallback="")
    try:
        return bytes(raw, "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")
    except (UnicodeDecodeError, UnicodeEncodeError):
        return raw


def askpass_helper():
    path = state_dir() / "ssh-askpass-exec.sh"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('#!/bin/sh\ncat "$AGRI_SSH_PASSWORD_FILE"\n', encoding="utf-8")
    path.chmod(0o700)
    return path


def write_password_file(password):
    path = state_dir() / "ssh-password-exec"
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".tmp")
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as handle:
        handle.write(password)
    tmp.replace(path)
    return path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--settings", default=str(Path.home() / ".config/Agri GCS/无人机地面站.conf"))
    parser.add_argument("--identity", default=str(Path.home() / ".ssh/agri_exec_ed25519"))
    parser.add_argument("--timeout", type=float, default=25.0)
    args = parser.parse_args()

    config = load_config(args.settings)
    host = setting(config, "host")
    user = setting(config, "sshUser") or "nv"
    if not host or host.startswith("rtsp://"):
        print("机载地址未配置", file=sys.stderr)
        return 2

    password = password_of(config)
    command = ["ssh", "-T", "-o", "ConnectTimeout=6", "-o", "StrictHostKeyChecking=accept-new",
               "-o", "ServerAliveInterval=5", "-o", "ServerAliveCountMax=2"]
    env = None
    if password:
        env = dict(os.environ)
        env["SSH_ASKPASS"] = str(askpass_helper())
        env["SSH_ASKPASS_REQUIRE"] = "force"
        env["AGRI_SSH_PASSWORD_FILE"] = str(write_password_file(password))
        command += ["-o", "PreferredAuthentications=password", "-o", "PubkeyAuthentication=no",
                    "-o", "NumberOfPasswordPrompts=1"]
    else:
        identity = Path(args.identity).expanduser()
        if not identity.is_file():
            print("SSH 执行密钥不存在：%s" % identity, file=sys.stderr)
            return 3
        command += ["-i", str(identity), "-o", "BatchMode=yes", "-o", "IdentitiesOnly=yes"]
    command += ["%s@%s" % (user, host), ACTION]

    try:
        result = subprocess.run(command, text=True, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=args.timeout)
    except subprocess.TimeoutExpired as error:
        print((error.output or "")[:800], file=sys.stderr)
        print("启动适配器超时", file=sys.stderr)
        return 124
    sys.stdout.write(result.stdout or "")
    return 0 if result.returncode == 0 else 4


if __name__ == "__main__":
    sys.exit(main())
