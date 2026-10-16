#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""SIYI 云台 UDP 中转服务（跑在机载电脑 / Jetson 上）—— v3

包含两个对外服务：

  1. UDP 37260 —— 云台 SDK 指令中转（地面站 ↔ 相机）
  2. TCP 37261 —— 相机的 HTTP 文件接口代理（照片/视频的列表与下载）

背景（实测踩坑记录，2026-10-07）：
  1. A8 mini 的 UDP SDK 是【单客户端】模型：它把会话绑在一个源端口上。
     v1 版本为每个下游客户端新建一条上游 socket，源端口不断变化，
     导致相机应答错乱并最终挂死（约 9 分钟无响应）。
     → v2 改为【全局唯一的上游 socket】，源端口恒定。
  2. SDK 文档明确写着心跳包（CMD 0x00）【仅 TCP 连接时支持】。
     实测对 UDP 发送 0x00 会让相机的命令服务挂死（TCP 端口也变为拒绝连接），
     恢复办法是发送 0x80 软重启并等待约 20~40 秒。
     → v2 直接【丢弃所有 0x00 帧】，不转发给相机。
  3. 拍照/录像的真实结果由相机通过 CMD 0x0B 主动回传，
     0x0C 本身是"无 ACK"的（地面站据此显示真实成败）。

相机文件接口（HTTP，无需认证）：
  GET http://192.168.144.25:82/cgi-bin/media.cgi/api/v1/getdirectories?media_type=0
  GET .../getmediacount?media_type=0&path=101SIYI_IMG
  GET .../getmedialist?media_type=0&path=101SIYI_IMG&start=0&count=20
  GET http://192.168.144.25:82/photo/101SIYI_IMG/IMG_0001.jpg   ← 实际文件

协议帧：55 66 | control | len(2,LE) | seq(2,LE) | cmd(1) | payload | crc16(2,LE)
"""
import argparse
import binascii
import http.server
import select
import socket
import struct
import sys
import threading
import time
import urllib.error
import urllib.request

CAM_ADDR = ("192.168.144.25", 37260)
CAM_HTTP = "http://192.168.144.25:82"     # 相机 Web Server（文件接口）
LISTEN_ADDR = ("0.0.0.0", 37260)
PROXY_ADDR = ("0.0.0.0", 37261)           # HTTP 代理监听（给地面站用）
CMD_HEARTBEAT = 0x00          # 仅 TCP 支持，UDP 发送会导致相机挂死，必须丢弃
CMD_ROTATE = 0x07             # 云台转向（流式速度指令，必须显式发 0 才停）
ROTATE_DEADMAN_SEC = 1.0      # 地面站超过这个时间没有新的转向指令，就替它发停止
CLIENT_IDLE_SEC = 600.0       # 超过这个时间没有客户端活动就不记录日志
HTTP_TIMEOUT_SEC = 30.0
HTTP_CHUNK = 65536


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


class CameraHttpProxy(http.server.BaseHTTPRequestHandler):
    """把地面站的 HTTP 请求透传给相机 Web Server（端口 82）。

    地面站与相机不同网段，照片/视频的列表与下载都只能经由机载电脑。
    这里只做只读透传：GET/HEAD 原样转发，路径照搬。
    """

    server_version = "siyi-cam-proxy/1.0"
    protocol_version = "HTTP/1.1"

    def _forward(self, send_body):
        target = CAM_HTTP + self.path
        try:
            request = urllib.request.Request(target, method="GET")
            with urllib.request.urlopen(request, timeout=HTTP_TIMEOUT_SEC) as response:
                self.send_response(response.status)
                content_type = response.headers.get("Content-Type", "application/octet-stream")
                self.send_header("Content-Type", content_type)
                length = response.headers.get("Content-Length")
                if length:
                    self.send_header("Content-Length", length)
                # 无论有没有 Content-Length 都断开连接：
                # 相机有时用 chunked 编码（没有 Content-Length），若保持
                # keep-alive，客户端会一直等响应体直到超时
                #（Qt 会报 "Operation canceled"，实测踩过）。
                self.send_header("Connection", "close")
                self.close_connection = True
                self.end_headers()
                if not send_body:
                    return
                total = 0
                while True:
                    chunk = response.read(HTTP_CHUNK)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    total += len(chunk)
                log("media-proxy: %s -> %d 字节" % (self.path[:70], total))
        except urllib.error.HTTPError as error:
            log("media-proxy: %s 相机返回 HTTP %s" % (self.path[:70], error.code))
            self.send_error(error.code, "camera returned %s" % error.code)
        except Exception as error:                      # noqa: BLE001
            log("media-proxy: %s 失败: %s" % (self.path[:70], error))
            try:
                self.send_error(502, "camera unreachable")
            except Exception:                           # noqa: BLE001
                pass

    def do_GET(self):                                   # noqa: N802
        self._forward(True)

    def do_HEAD(self):                                  # noqa: N802
        self._forward(False)

    def log_message(self, fmt, *args):
        # 默认实现会往 stderr 打日志，这里改走统一日志格式
        if "code" not in fmt:
            log("media-proxy: " + (fmt % args))


def start_http_proxy():
    server = http.server.ThreadingHTTPServer(PROXY_ADDR, CameraHttpProxy)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, name="camera-http-proxy", daemon=True)
    thread.start()
    return server


def main():
    parser = argparse.ArgumentParser(description="SIYI A8 mini 云台/媒体中转")
    parser.add_argument("--no-http", action="store_true",
                        help="不启动 HTTP 媒体代理（只做云台 UDP 转发）")
    parser.add_argument("--http-only", action="store_true",
                        help="只启动 HTTP 媒体代理（照片/录像文件），不做 UDP 转发")
    args = parser.parse_args()

    # ⚠️ 为什么要把 HTTP 代理拆出去单独跑一个进程：
    # 照片/录像下载是几十 MB 的流量，如果和云台 UDP 转发挤在同一个 Python
    # 进程里，HTTP 线程会抢 GIL，把转发主循环拖住 —— 表现为"一点图片列表，
    # 云台操作杆就延迟极大"（2026-10-15 实际复现）。
    # 拆成两个进程后，下载再重也不影响云台指令的实时性。
    if args.http_only:
        start_http_proxy()
        log("HTTP-only 模式：只提供照片/录像文件代理，不转发云台指令")
        log("  监听 %s:%d → %s" % (PROXY_ADDR[0], PROXY_ADDR[1], CAM_HTTP))
        try:
            while True:
                time.sleep(1.0)
        except KeyboardInterrupt:
            pass
        return

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

    log("siyi-gimbal-relay v3 启动")
    log("  监听 %s:%d（UDP，云台指令）" % LISTEN_ADDR)
    log("  上游 %s:%d（单一持久 socket，本地端口 %d）"
        % (CAM_ADDR[0], CAM_ADDR[1], upstream.getsockname()[1]))
    log("  安全策略: 丢弃 CMD 0x00（心跳，UDP 不支持）")
    log("  安全策略: CMD 0x07 转向死手保护 %.1f 秒（地面站失联自动停转）"
        % ROTATE_DEADMAN_SEC)
    if args.no_http:
        log("  媒体代理: 已禁用（--no-http，由独立的 siyi-media-proxy 服务提供）")
    else:
        try:
            start_http_proxy()
            log("  HTTP 代理 %s:%d → %s（照片/视频文件接口，只读）"
                % (PROXY_ADDR[0], PROXY_ADDR[1], CAM_HTTP))
        except OSError as error:
            log("!! HTTP 代理启动失败（端口被占用？）: %s" % error)

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
