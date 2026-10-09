#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""验证 SDK 0x07 云台转向的符号约定与停止行为。

文档说：向右滑动 0~100（yaw 正），向上滑动 0~100（pitch 正），松手发 0 停止。
本脚本实测确认，并检查"不发 0 会不会自己停"。
"""
import binascii
import socket
import struct
import time

RELAY = ("192.168.2.113", 37260)
HDR = b"\x55\x66"


def crc16(d):
    return binascii.crc_hqx(d, 0)


def frame(cmd, payload=b"", seq=0):
    body = (HDR + b"\x01" + struct.pack("<H", len(payload))
            + struct.pack("<H", seq) + bytes((cmd,)) + payload)
    return body + struct.pack("<H", crc16(body))


class G:
    def __init__(self):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.settimeout(2.5)
        self.seq = 0

    def cmd(self, cmd, payload=b"", wait=True):
        self.seq += 1
        self.s.sendto(frame(cmd, payload, self.seq), RELAY)
        if not wait:
            return None
        deadline = time.time() + 2.5
        while time.time() < deadline:
            try:
                d, _ = self.s.recvfrom(2048)
            except socket.timeout:
                break
            if len(d) < 10 or d[:2] != HDR:
                continue
            if d[7] != cmd:
                continue
            return d[8:-2]
        return None

    def attitude(self):
        r = self.cmd(0x0D)
        if r and len(r) >= 6:
            y, p, ro = struct.unpack_from("<hhh", r, 0)
            return y / 10.0, p / 10.0
        return None

    def rotate(self, yaw, pitch, wait=False):
        """int8 有符号：负值要转成两补码"""
        payload = bytes(((yaw & 0xFF), (pitch & 0xFF)))
        return self.cmd(0x07, payload, wait=wait)


g = G()
print("=== 0) 初始姿态 ===")
a0 = g.attitude()
print("   ", a0)

print("\n=== 1) yaw +50（文档：向右）转动 1.2 秒后停止 ===")
g.rotate(50, 0)
time.sleep(1.2)
g.rotate(0, 0, wait=False)
time.sleep(0.6)
a1 = g.attitude()
print("    转向后:", a1, " → yaw 变化:", None if not a1 else round(a1[0] - a0[0], 1))

print("\n=== 2) yaw -50（文档：向左）转动 1.2 秒后停止 ===")
g.rotate(-50, 0)
time.sleep(1.2)
g.rotate(0, 0, wait=False)
time.sleep(0.6)
a2 = g.attitude()
print("    转向后:", a2, " → yaw 变化:", None if not a2 else round(a2[0] - a1[0], 1))

print("\n=== 3) pitch +50（文档：向上）转动 1.0 秒后停止 ===")
g.rotate(0, 50)
time.sleep(1.0)
g.rotate(0, 0, wait=False)
time.sleep(0.6)
a3 = g.attitude()
print("    转向后:", a3, " → pitch 变化:", None if not a3 else round(a3[1] - a2[1], 1))

print("\n=== 4) pitch -50（文档：向下）转动 1.0 秒后停止 ===")
g.rotate(0, -50)
time.sleep(1.0)
g.rotate(0, 0, wait=False)
time.sleep(0.6)
a4 = g.attitude()
print("    转向后:", a4, " → pitch 变化:", None if not a4 else round(a4[1] - a3[1], 1))

print("\n=== 5) 关键安全测试：发一个速度后【不发 0】，看它会不会自己停 ===")
before = g.attitude()
g.rotate(60, 0, wait=False)
print("    已发 yaw +60，等待 2 秒（不发 0）...")
time.sleep(2.0)
mid = g.attitude()
print("    2 秒后:", mid)
g.rotate(0, 0, wait=False)
time.sleep(0.5)
after = g.attitude()
print("    发 0 之后:", after)
print("    → 判断：", end="")
if mid and after and abs(after[0] - mid[0]) < 0.5:
    print("相机【保持】速度直到收到 0（必须显式停止！）")
elif mid and after and abs(after[0] - mid[0]) >= 0.5:
    print("相机会自己减速/停止")
else:
    print("数据不足")

print("\n=== 6) 回中恢复 ===")
g.cmd(0x08, b"\x01", wait=False)
time.sleep(2.0)
print("    回中后:", g.attitude())
g.s.close()
