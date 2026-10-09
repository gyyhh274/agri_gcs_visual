#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""经中转验证云台各项指令（与地面站使用完全相同的链路）。

注意：本脚本【不会】发送 0x00 心跳（UDP 不支持，会让相机挂死）。
"""
import binascii
import socket
import struct
import time

RELAY = ("192.168.2.113", 37260)   # 地面站看到的就是这个地址
HDR = b"\x55\x66"


def crc16(data):
    return binascii.crc_hqx(data, 0)


def frame(cmd, payload=b"", seq=0):
    body = (HDR + b"\x01" + struct.pack("<H", len(payload))
            + struct.pack("<H", seq) + bytes((cmd,)) + payload)
    return body + struct.pack("<H", crc16(body))


class Gimbal:
    def __init__(self):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.settimeout(2.5)
        self.seq = 0

    def cmd(self, cmd, payload=b"", expect=True, name=""):
        self.seq += 1
        self.s.sendto(frame(cmd, payload, self.seq), RELAY)
        if not expect:
            print("  → %-16s CMD 0x%02X payload=%s（无应答）" % (name, cmd, payload.hex(" ") or "-"))
            return None
        deadline = time.time() + 2.5
        while time.time() < deadline:
            try:
                d, _ = self.s.recvfrom(2048)
            except socket.timeout:
                break
            if len(d) < 10 or d[:2] != HDR:
                continue
            plen = struct.unpack_from("<H", d, 3)[0]
            if len(d) != 8 + plen + 2:
                continue
            if crc16(d[:-2]) != struct.unpack_from("<H", d, len(d) - 2)[0]:
                continue
            cid, payload_resp = d[7], d[8:-2]
            if cid != cmd:
                continue
            print("  ← %-16s CMD 0x%02X payload=%s" % (name, cid, payload_resp.hex(" ") or "-"))
            return payload_resp
        print("  ✘ %-16s 无应答" % name)
        return None

    def attitude(self):
        r = self.cmd(0x0D, name="获取姿态")
        if r and len(r) >= 6:
            y, p, ro = struct.unpack_from("<hhh", r, 0)
            return y / 10.0, p / 10.0, ro / 10.0
        return None

    def record_status(self):
        r = self.cmd(0x0A, name="录像状态")
        return None if not r or len(r) < 4 else r[3]

    def mode(self):
        r = self.cmd(0x19, name="云台模式")
        return None if not r else r[0]

    def zoom(self):
        r = self.cmd(0x18, name="当前变倍")
        return None if not r or len(r) < 2 else r[0] + r[1] / 10.0


def main():
    g = Gimbal()
    print("目标（中转）:", RELAY, "\n")

    print("=========== 1) 基线状态 ===========")
    print("  姿态:", g.attitude())
    print("  录像状态码:", g.record_status())
    print("  模式:", g.mode())
    print("  变倍:", g.zoom())

    print("\n=========== 2) 拍照（0x0C/00）===========")
    r = g.cmd(0x0C, b"\x00", name="拍照")
    if r and len(r) >= 1:
        info = r[0]
        names = {0: "拍照成功", 1: "拍照失败（未插 TF 卡）", 2: "HDR 开", 3: "HDR 关",
                 4: "录像失败（未插 TF 卡）", 5: "开始录像", 6: "结束录像"}
        print("    → 结果:", names.get(info, "未知(%d)" % info))

    print("\n=========== 3) 录像开关（0x0C/02，切换式）===========")
    before = g.record_status()
    print("  切换前状态码:", before)
    g.cmd(0x0C, b"\x02", expect=False, name="录像切换")
    time.sleep(2.0)
    after = g.record_status()
    print("  切换后状态码:", after, "(1=录像中 0=未录像 2=无TF卡)")
    # 复原
    g.cmd(0x0C, b"\x02", expect=False, name="录像复原")
    time.sleep(2.0)
    print("  复原后状态码:", g.record_status())

    print("\n=========== 4) 绝对变倍（0x0F，设 2.0x 再复原）===========")
    print("  当前:", g.zoom())
    g.cmd(0x0F, bytes((2, 0)), name="设 2.0x")
    time.sleep(1.5)
    print("  设置后:", g.zoom())
    print("  最大变倍:", g.cmd(0x16, name="最大变倍"))
    g.cmd(0x0F, bytes((1, 0)), name="复原 1.0x")
    time.sleep(1.5)
    print("  复原后:", g.zoom())

    print("\n=========== 5) 模式切换（0x0C/04 跟随 → 0x0C/03 锁定）===========")
    print("  当前模式:", g.mode())
    g.cmd(0x0C, b"\x04", expect=False, name="跟随模式")
    time.sleep(1.5)
    print("  切换后:", g.mode(), "(1=跟随)")
    g.cmd(0x0C, b"\x03", expect=False, name="锁定模式")
    time.sleep(1.5)
    print("  复原后:", g.mode(), "(0=锁定)")

    print("\n=========== 6) 最终状态 ===========")
    print("  姿态:", g.attitude())
    print("  录像:", g.record_status(), " 模式:", g.mode(), " 变倍:", g.zoom())
    g.s.close()
    print("\n验证完成（所有可逆操作均已复原）")


if __name__ == "__main__":
    main()
