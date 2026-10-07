#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""SIYI 云台 UDP 中转服务（跑在机载电脑 / Jetson 上）—— v2

背景（实测踩坑记录，2026-10-07）：
  1. A8 mini 的 UDP SDK 是【单客户端】模型：它把会话绑在一个源端口上。
     v1 版本为每个下游客户端新建一条上游 socket，源端口不断变化，
     导致相机应答错乱并最终挂死（约 9 分钟无响应）。
     → v2 改为【全局唯一的上游 socket】，源端口恒定。
  2. SDK 文档明确写着心跳包（CMD 0x00）【仅 TCP 连接时支持】。
     实测对 UDP 发送 0x00 会让相机的命令服务挂死（TCP 端口也变为拒绝连接），
     恢复办法是发送 0x80 软重启并等待约 20~40 秒。
     → v2 直接【丢弃所有 0x00 帧】，不转发给相机。

协议帧：55 66 | control | len(2,LE) | seq(2,LE) | cmd(1) | payload | crc16(2,LE)
"""
import select
import socket
import struct
import sys
import time
import binascii

CAM_ADDR = ("192.168.144.25", 37260)
LISTEN_ADDR = ("0.0.0.0", 37260)
CMD_HEARTBEAT = 0x00          # 仅 TCP 支持，UDP 发送会导致相机挂死，必须丢弃
CMD_ROTATE = 0x07             # 云台转向（流式速度指令，必须显式发 0 才停）
ROTATE_DEADMAN_SEC = 1.0      # 地面站超过这个时间没有新的转向指令，就替它发停止
CLIENT_IDLE_SEC = 600.0       # 超过这个时间没有客户端活动就不记录日志


def log(msg):
    print(time.strftime("[%Y-%m-%d %H:%M:%S] ") + msg, flush=True)


def command_id(data):
    """从帧里取命令号；解析失败返回 None。"""
    if len(data) < 8 or data[:2] != b"\x55\x66":
        return None
    return data[7]


def crc16(data):
    return binascii.crc_hqx(data, 0)


def build_frame(cmd, payload=b""):
    """构造一帧 SDK 指令（用途：替失联的地面站补发转向停止）。"""
    body = (b"\x55\x66\x01" + struct.pack("<H", len(payload))
            + struct.pack("<H", 0) + bytes((cmd,)) + payload)
    return body + struct.pack("<H", crc16(body))


def rotate_is_active(data):
    """0x07 的载荷是两个 int8 速度，非零即表示正在转向。"""
    return len(data) >= 10 and (data[8] != 0 or data[9] != 0)


def main():
    downstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    downstream.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    downstream.bind(LISTEN_ADDR)
    downstream.setblocking(False)

    # 唯一的、长期存活的上游 socket：源端口恒定，符合相机单客户端模型
    upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    upstream.setblocking(False)

    active_client = None
    last_drop_log = 0.0
    forward_count = 0
    reply_count = 0
    rotate_active = False        # 相机当前是否被命令持续转向
    last_rotate_at = 0.0

    log("siyi-gimbal-relay v2 启动")
    log("  监听 %s:%d（UDP）" % LISTEN_ADDR)
    log("  上游 %s:%d（单一持久 socket，本地端口 %d）"
        % (CAM_ADDR[0], CAM_ADDR[1], upstream.getsockname()[1]))
    log("  安全策略: 丢弃 CMD 0x00（心跳，UDP 不支持）")
    log("  安全策略: CMD 0x07 转向死手保护 %.1f 秒（地面站失联自动停转）"
        % ROTATE_DEADMAN_SEC)

    while True:
        readable, _, _ = select.select([downstream, upstream], [], [], 0.2)

        # 拨杆死手保护：地面站崩溃/断网时，替它补发转向停止，
        # 否则相机会一直朝上次的速度转下去。
        if rotate_active and time.time() - last_rotate_at > ROTATE_DEADMAN_SEC:
            try:
                upstream.sendto(build_frame(CMD_ROTATE, b"\x00\x00"), CAM_ADDR)
                rotate_active = False
                log("!! 拨杆死手保护触发：%.1f 秒无转向指令，已代替地面站发送停止"
                    % ROTATE_DEADMAN_SEC)
            except OSError as error:
                log("!! 死手保护发送失败: %s" % error)

        for sock in readable:
            if sock is downstream:
                data, client = downstream.recvfrom(4096)
                cmd = command_id(data)
                if cmd == CMD_HEARTBEAT:
                    now = time.time()
                    if now - last_drop_log > 10:
                        log("已丢弃心跳帧（CMD 0x00，来自 %s:%d）—— UDP 不支持心跳"
                            % (client[0], client[1]))
                        last_drop_log = now
                    continue
                active_client = client
                # 0x07 转向：记录是否处于"持续转向"状态，供死手保护使用
                if cmd == CMD_ROTATE:
                    last_rotate_at = time.time()
                    rotate_active = rotate_is_active(data)
                try:
                    upstream.sendto(data, CAM_ADDR)
                    forward_count += 1
                    log("-> #%d 转发 CMD 0x%02X（%d 字节）给相机，来自 %s:%d"
                        % (forward_count, cmd if cmd is not None else -1,
                           len(data), client[0], client[1]))
                except OSError as error:
                    log("!! 转发失败: %s" % error)
            else:
                try:
                    data, source = upstream.recvfrom(4096)
                except OSError:
                    continue
                reply_count += 1
                if source[0] != CAM_ADDR[0]:
                    log("!! 忽略非相机来源 %s:%d" % (source[0], source[1]))
                    continue
                if active_client is None:
                    log("!! 收到相机应答但没有活跃客户端，丢弃")
                    continue
                try:
                    downstream.sendto(data, active_client)
                    log("<- #%d 回传 CMD 0x%02X（%d 字节）给 %s:%d"
                        % (reply_count, command_id(data) or -1, len(data),
                           active_client[0], active_client[1]))
                except OSError as error:
                    log("!! 回传失败: %s" % error)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
