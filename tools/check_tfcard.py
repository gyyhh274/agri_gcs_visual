#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""插卡后验证：状态 → 拍照 → 文件列表 → 下载"""
import binascii
import json
import os
import socket
import struct
import subprocess
import time

CAM_IP = "192.168.144.25"
HTTP = "http://%s:82/cgi-bin/media.cgi/api/v1" % CAM_IP
INFO = {0: "拍照成功", 1: "拍照失败（未插 TF 卡）", 4: "录像失败（未插 TF 卡）",
        5: "开始录像", 6: "结束录像"}


def crc16(d):
    return binascii.crc_hqx(d, 0)


def frame(cmd, payload=b"", seq=1):
    body = (b"\x55\x66\x01" + struct.pack("<H", len(payload))
            + struct.pack("<H", seq) + bytes((cmd,)) + payload)
    return body + struct.pack("<H", crc16(body))


def get(url):
    return subprocess.run(["curl", "-s", "--max-time", "12", url],
                          capture_output=True).stdout.decode("utf-8", "replace")


s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(6.0)


def ask(cmd, payload=b"", label="", watch=5.0):
    print("  → %s" % (label or ("CMD 0x%02X" % cmd)))
    s.sendto(frame(cmd, payload, 1), (CAM_IP, 37260))
    out = []
    end = time.time() + watch
    while time.time() < end:
        try:
            d, _ = s.recvfrom(2048)
        except socket.timeout:
            break
        if len(d) >= 10 and d[:2] == b"\x55\x66":
            c, p = d[7], d[8:-2]
            print("     ← CMD 0x%02X payload=%s" % (c, p.hex(" ") or "-"))
            out.append((c, p))
    return out


print("=== 1) 系统信息（看 record_sta 是否还是 2）===")
r = ask(0x0A, b"", "请求系统信息", 3.0)
for c, p in r:
    if c == 0x0A and len(p) >= 4:
        sta = p[3]
        names = {0: "未录像", 1: "录像中", 2: "★未插TF卡", 3: "TF卡丢数据"}
        print("     record_sta = %d → %s" % (sta, names.get(sta, "?")))

print()
print("=== 2) 拍照，看 0x0B 反馈 ===")
r = ask(0x0C, b"\x00", "拍照", 8.0)
for c, p in r:
    if c == 0x0B and p:
        print("     ★ 结果: info_type=%d → %s" % (p[0], INFO.get(p[0], "未知")))

print()
print("=== 3) 等 3 秒后查文件列表 ===")
time.sleep(3.0)
raw = get("%s/getmediacount?media_type=0&path=101SIYI_IMG" % HTTP)
print("   计数:", raw.strip().replace("\n", "").replace("\t", ""))
raw = get("%s/getmedialist?media_type=0&path=101SIYI_IMG&start=0&count=5" % HTTP)
print("   列表:", raw.strip().replace("\n", "").replace("\t", "")[:400])

print()
print("=== 4) 若有文件则下载最新一张 ===")
try:
    data = json.loads(get("%s/getmedialist?media_type=0&path=101SIYI_IMG&start=0&count=5" % HTTP))
    lst = data.get("data", {}).get("list", [])
    if lst:
        f = lst[0]
        url = f["url"]
        print("   最新文件:", f["name"], "URL:", url)
        if ":" not in url.split("//", 1)[1].split("/", 1)[0]:
            url = url.replace("http://%s" % CAM_IP, "http://%s:82" % CAM_IP)
            print("   补端口:", url)
        out = "/tmp/pulled_%s" % f["name"]
        subprocess.run(["curl", "-s", "--max-time", "40", "-o", out, url], capture_output=True)
        if os.path.exists(out) and os.path.getsize(out) > 0:
            head = open(out, "rb").read(4)
            print("   ✔ 下载成功: %s (%d 字节, JPEG=%s)"
                  % (out, os.path.getsize(out), head[:2] == b"\xff\xd8"))
        else:
            print("   ✘ 下载失败")
    else:
        print("   列表仍为空")
except Exception as e:
    print("   解析失败:", e)
s.close()
