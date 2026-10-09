#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""验证完整拍照取回流程：拍照 → 文件列表出现新文件 → 下载并校验。

只读取和下载，不删除相机上的任何文件。
"""
import binascii
import json
import os
import socket
import struct
import subprocess
import time

CAM_IP = "192.168.144.25"
HTTP = "http://%s:82/cgi-bin/media.cgi/api/v1" % CAM_IP
PHOTO_DIR = "101SIYI_IMG"


def crc16(d):
    return binascii.crc_hqx(d, 0)


def frame(cmd, payload=b"", seq=1):
    body = (b"\x55\x66\x01" + struct.pack("<H", len(payload))
            + struct.pack("<H", seq) + bytes((cmd,)) + payload)
    return body + struct.pack("<H", crc16(body))


def http_get(url, timeout=12):
    """用 curl 取（Python 在 Jetson 上不一定有 requests）"""
    r = subprocess.run(["curl", "-s", "--max-time", str(timeout), url],
                       capture_output=True)
    return r.stdout


def media_list(count=5):
    url = "%s/getmedialist?media_type=0&path=%s&start=0&count=%d" % (HTTP, PHOTO_DIR, count)
    raw = http_get(url)
    try:
        return json.loads(raw)
    except Exception:
        return {"raw": raw[:200].decode("utf-8", "replace")}


def media_count():
    raw = http_get("%s/getmediacount?media_type=0&path=%s" % (HTTP, PHOTO_DIR))
    try:
        return json.loads(raw)
    except Exception:
        return {"raw": raw[:200].decode("utf-8", "replace")}


print("=== 1) 拍照前的文件个数与最新文件 ===")
c0 = media_count()
print("   计数响应:", json.dumps(c0, ensure_ascii=False)[:200])
l0 = media_list(3)
before_names = []
if "data" in l0 and "list" in l0["data"]:
    before_names = [f["name"] for f in l0["data"]["list"]]
    print("   最新 3 个:", before_names)
    if l0["data"]["list"]:
        print("   文件 URL 示例:", l0["data"]["list"][0].get("url"))

print()
print("=== 2) 下发电机拍照指令（0x0C/00）===")
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.sendto(frame(0x0C, b"\x00", 1), (CAM_IP, 37260))
print("   已发送")
s.close()

print()
print("=== 3) 轮询看新文件多久出现 ===")
new_file = None
for i in range(12):
    time.sleep(1.0)
    l = media_list(3)
    names = []
    if "data" in l and "list" in l["data"]:
        names = [f["name"] for f in l["data"]["list"]]
    fresh = [n for n in names if n not in before_names]
    print("   +%2ds: %s %s" % (i + 1, names[:3], ("★ 新文件: " + str(fresh)) if fresh else ""))
    if fresh:
        new_file = fresh[0]
        break

if not new_file:
    print("\n   ✘ 12 秒内没看到新文件")
    raise SystemExit(1)

print()
print("=== 4) 下载新文件 ===")
l = media_list(3)
url = None
for f in l["data"]["list"]:
    if f["name"] == new_file:
        url = f["url"]
        break
print("   URL:", url)
if url:
    # 文档里的 url 可能不带端口，补上 :82
    if ":" not in url.split("//", 1)[1].split("/", 1)[0]:
        url = url.replace("http://%s" % CAM_IP, "http://%s:82" % CAM_IP)
        print("   补端口后:", url)
    out = "/tmp/pulled_%s" % new_file
    r = subprocess.run(["curl", "-s", "--max-time", "30", "-o", out, url], capture_output=True)
    if os.path.exists(out):
        size = os.path.getsize(out)
        head = open(out, "rb").read(4)
        is_jpeg = head[:2] == b"\xff\xd8"
        print("   下载完成: %s (%d 字节, JPEG=%s)" % (out, size, is_jpeg))
    else:
        print("   ✘ 下载失败")

print()
print("=== 5) 计数复查 ===")
print("   ", json.dumps(media_count(), ensure_ascii=False)[:200])
