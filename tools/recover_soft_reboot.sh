#!/usr/bin/env bash
# 最后的软件恢复尝试：SDK 0x80 云台相机软重启（载荷 = 相机重启 + 云台复位）
set -uo pipefail

python3 - <<'PY'
import binascii, socket, struct, time

def crc16(d):
    return binascii.crc_hqx(d, 0)

def frame(cmd, payload=b"", seq=0):
    body = (b"\x55\x66\x01" + struct.pack("<H", len(payload))
            + struct.pack("<H", seq) + bytes((cmd,)) + payload)
    return body + struct.pack("<H", crc16(body))

CAM = ("192.168.144.25", 37260)
REBOOT = frame(0x80, b"\x01\x01")      # 相机重启 + 云台复位
print("软重启帧:", REBOOT.hex(" "))

print("\n=== 1) UDP 发送软重启（重复 5 次，间隔 1s）===")
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(2.0)
for i in range(1, 6):
    s.sendto(REBOOT, CAM)
    try:
        d, _ = s.recvfrom(2048)
        print("  第%d次 ✔ 收到应答: %s" % (i, d.hex(" ")))
        break
    except socket.timeout:
        print("  第%d次 ✘ 无应答" % i)
    time.sleep(1.0)
s.close()

print("\n=== 2) TCP 发送软重启 ===")
try:
    t = socket.create_connection(CAM, timeout=4)
    t.sendall(REBOOT)
    t.settimeout(3)
    try:
        d = t.recv(2048)
        print("  ✔ 收到应答:", d.hex(" "))
    except socket.timeout:
        print("  ✘ 无应答")
    t.close()
except Exception as e:
    print("  ✘ 连接失败:", e)

print("\n=== 3) 等 40 秒后看是否重启完成 ===")
time.sleep(40)
u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); u.settimeout(3)
body = b"\x55\x66\x01" + struct.pack("<H",0) + struct.pack("<H",0) + b"\x0d"
pkt = body + struct.pack("<H", crc16(body))
for i in range(1, 4):
    u.sendto(pkt, CAM)
    try:
        d, _ = u.recvfrom(2048)
        y, p, r = struct.unpack_from("<hhh", d, 8)
        print("  ✔ 控制通道恢复! yaw=%.1f pitch=%.1f roll=%.1f" % (y/10, p/10, r/10))
        break
    except socket.timeout:
        print("  第%d次仍无响应" % i)
        time.sleep(5)
u.close()
PY

echo
echo "=== 4) 视频链路是否还在 ==="
timeout 12 gst-launch-1.0 -q rtspsrc location=rtsp://192.168.144.25:8554/main.264 \
  protocols=tcp ! fakesink num-buffers=10 2>&1 | tail -2
echo "  RTSP 退出码=$?"
