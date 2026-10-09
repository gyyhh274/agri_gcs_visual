#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""诊断云台是否卡在"持续转向"状态，并强制停止。

背景：0x07 转向是流式指令，若停止包 (0,0) 丢失，相机会一直转。
"""
import binascii
import socket
import struct
import time

RELAY = ("192.168.2.113", 37260)


def crc16(d):
    return binascii.crc_hqx(d, 0)


def frame(cmd, payload=b"", seq=0):
    body = (b"\x55\x66\x01" + struct.pack("<H", len(payload))
            + struct.pack("<H", seq) + bytes((cmd,)) + payload)
    return body + struct.pack("<H", crc16(body))


class G:
    def __init__(self):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.settimeout(2.0)
        self.seq = 0

    def cmd(self, cmd, payload=b"", wait=True):
        self.seq += 1
        self.s.sendto(frame(cmd, payload, self.seq), RELAY)
        if not wait:
            return None
        deadline = time.time() + 2.0
        while time.time() < deadline:
            try:
                d, _ = self.s.recvfrom(2048)
            except socket.timeout:
                break
            if len(d) >= 10 and d[:2] == b"\x55\x66" and d[7] == cmd:
                return d[8:-2]
        return None

    def attitude(self, tries=3):
        for _ in range(tries):
            r = self.cmd(0x0D)
            if r and len(r) >= 6:
                y, p, ro = struct.unpack_from("<hhh", r, 0)
                return y / 10.0, p / 10.0
            time.sleep(0.3)
        return None


g = G()

print("=== 1) 连续采样 5 次（间隔 0.8 秒），看是否在自行转动 ===")
samples = []
for i in range(5):
    a = g.attitude()
    samples.append(a)
    print("  #%d %s" % (i + 1, a))
    time.sleep(0.8)

moving = False
if all(samples) and len(samples) >= 2:
    deltas = [abs(samples[i + 1][0] - samples[i][0]) for i in range(len(samples) - 1)]
    print("  yaw 相邻变化量:", [round(d, 1) for d in deltas])
    moving = max(deltas) > 2.0
print("  判定:", "★ 云台正在自行转动（卡在转向状态）" if moving else "云台静止")

print("\n=== 2) 强制发送停止指令（连发 5 次确保到达）===")
for i in range(5):
    g.cmd(0x07, b"\x00\x00", wait=False)
    time.sleep(0.2)
print("  已发送 5 次 (0,0)")
time.sleep(1.0)

print("\n=== 3) 复查是否停下 ===")
after = []
for i in range(4):
    a = g.attitude()
    after.append(a)
    print("  #%d %s" % (i + 1, a))
    time.sleep(0.8)
if all(after) and len(after) >= 2:
    deltas = [abs(after[i + 1][0] - after[i][0]) for i in range(len(after) - 1)]
    print("  yaw 相邻变化量:", [round(d, 1) for d in deltas])
    print("  判定:", "✔ 已停止" if max(deltas) <= 2.0 else "✘ 仍在转动")

print("\n=== 4) 停止后再试绝对角度控制 ===")
a = g.attitude()
if a:
    print("  当前:", a)
    target_yaw = a[0]
    target_pitch = a[1] - 5.0
    print("  下发 yaw=%.1f pitch=%.1f" % (target_yaw, target_pitch))
    r = g.cmd(0x0E, struct.pack("<hh", int(target_yaw * 10), int(target_pitch * 10)))
    if r and len(r) >= 4:
        y, p = struct.unpack_from("<hh", r, 0)
        print("  ACK: yaw=%.1f pitch=%.1f" % (y / 10, p / 10))
    time.sleep(1.5)
    print("  复查:", g.attitude())

print("\n=== 5) 复位到回中 ===")
g.cmd(0x08, b"\x01", wait=False)
time.sleep(2.5)
print("  回中后:", g.attitude())
g.s.close()
