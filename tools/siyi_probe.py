#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""在 Jetson 上直接探测 A8 mini 云台（UDP 37260）。
只做只读查询，不发送任何运动指令。
"""
import binascii
import socket
import struct
import sys
import time

# 默认直连相机；传入参数则指向中转地址（如 192.168.2.113）
CAM_HOST = sys.argv[1] if len(sys.argv) > 1 else "192.168.144.25"
CAM = (CAM_HOST, 37260)
HDR = b"\x55\x66"


def crc16(data):
    """SIYI CRC16: CCITT poly 0x1021, init 0."""
    return binascii.crc_hqx(data, 0)


def frame(cmd, payload=b"", seq=0, ack=True):
    body = (HDR + bytes((1 if ack else 0,))
            + struct.pack("<H", len(payload))
            + struct.pack("<H", seq)
            + bytes((cmd,)) + payload)
    return body + struct.pack("<H", crc16(body))


def send(sock, name, cmd, payload=b"", expect=True, timeout=2.0):
    pkt = frame(cmd, payload)
    sock.sendto(pkt, CAM)
    print("发送 %-10s cmd=0x%02X payload=%-10s frame=%s"
          % (name, cmd, payload.hex(" ") or "-", pkt.hex(" ")))
    if not expect:
        return None
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            data, src = sock.recvfrom(2048)
        except socket.timeout:
            break
        if len(data) < 10 or data[:2] != HDR:
            continue
        plen = struct.unpack_from("<H", data, 3)[0]
        if len(data) != 8 + plen + 2:
            continue
        if crc16(data[:-2]) != struct.unpack_from("<H", data, len(data) - 2)[0]:
            print("   [丢弃] CRC 校验失败")
            continue
        print("  收到 cmd=0x%02X len=%d payload=%s"
              % (data[7], len(data[8:-2]), data[8:-2].hex(" ") or "-"))
        return data[8:-2]
    print("  !! 超时无响应")
    return None


def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(2.0)
    print("目标: %s:%d\n" % CAM)

    print("=========== 1) 云台状态信息 (0x0A) ===========")
    r = send(sock, "云台状态", 0x0A)
    if r and len(r) >= 4:
        names = {0: "停止录像", 1: "正在录像", 2: "无TF卡", 3: "录像丢数据"}
        print("   录像状态字节[3]=%d -> %s" % (r[3], names.get(r[3], "未知")))
        print("   完整载荷:", r.hex(" "))

    print("\n=========== 2) 获取云台姿态 (0x0D) ===========")
    r = send(sock, "获取姿态", 0x0D)
    if r and len(r) >= 6:
        yaw, pitch, roll = struct.unpack_from("<hhh", r, 0)
        print("   yaw=%.1f°  pitch=%.1f°  roll=%.1f°" % (yaw / 10.0, pitch / 10.0, roll / 10.0))

    print("\n=========== 3) 当前云台模式 (0x19) ===========")
    r = send(sock, "当前模式", 0x19)
    if r and len(r) >= 1:
        modes = {0: "锁定", 1: "跟随", 2: "FPV"}
        print("   模式=%d -> %s" % (r[0], modes.get(r[0], "未知")))

    print("\n=========== 4) 当前变倍值 (0x18) ===========")
    r = send(sock, "当前变倍", 0x18)
    if r and len(r) >= 2:
        print("   原始载荷:", r.hex(" "))

    print("\n=========== 5) 最大变倍值 (0x16) ===========")
    r = send(sock, "最大变倍", 0x16)
    if r and len(r) >= 2:
        print("   原始载荷:", r.hex(" "))

    print("\n=========== 6) 心跳包（SDK 文档给的标准帧）===========")
    hb = bytes.fromhex("55660101000000000059 8B".replace(" ", ""))
    sock.sendto(hb, CAM)
    print("   发送:", hb.hex(" "))
    try:
        data, _ = sock.recvfrom(2048)
        print("   收到:", data.hex(" "))
    except socket.timeout:
        print("   （心跳无响应属正常，部分固件不回）")

    sock.close()
    print("\n探测完成（未发送任何运动指令）")


if __name__ == "__main__":
    main()
