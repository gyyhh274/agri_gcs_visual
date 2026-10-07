# 机载电脑（Jetson）部署说明

地面站要连的两样东西都在机载电脑上：**视频中转（MediaMTX）** 和
**云台控制中转（siyi-gimbal-relay）**。相机在 `192.168.144.0/24` 网段，
只有机载电脑能直达，地面站通过 Wi-Fi 访问机载电脑。

```text
地面站                        机载电脑 192.168.2.113               相机 192.168.144.25
  ├─ RTSP 8554  ──────────►  MediaMTX（视频中转）  ──────────►  :8554/main.264 (H.265)
  └─ UDP 37260  ──────────►  siyi-gimbal-relay    ──────────►  :37260 (SIYI SDK)
```

## 1. 云台控制中转

| 文件 | 部署位置 |
| --- | --- |
| `siyi_gimbal_relay.py` | `/usr/local/bin/siyi_gimbal_relay.py` |
| `siyi-gimbal-relay.service` | `/etc/systemd/system/` |

```bash
sudo install -m 755 siyi_gimbal_relay.py /usr/local/bin/siyi_gimbal_relay.py
sudo install -m 644 siyi-gimbal-relay.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now siyi-gimbal-relay
systemctl status siyi-gimbal-relay
```

### 它解决的两个问题

1. **跨网段**：相机只在 `192.168.144.0/24`，地面站够不着，必须中转。
2. **单客户端模型**：A8 mini 的 UDP SDK 把会话绑在源端口上。
   中转用**全局唯一的上游 socket**（源端口恒定），所有地面站客户端共用它。

### 内置的两条安全策略

| 策略 | 原因 |
| --- | --- |
| **丢弃 CMD 0x00（心跳）** | 心跳仅 TCP 支持；对 UDP 发送会让相机命令服务挂死约 9 分钟 |
| **CMD 0x07 转向死手保护（1 秒）** | 转向是流式指令，地面站崩溃/断网时会一直转；中转替它发停止 |

### 部署后必做的自检

```bash
# 1) 服务在跑
systemctl is-active siyi-gimbal-relay

# 2) 跑的是新版本（别只看 active！cp 忘了 sudo 会静默失败）
grep -c '死手保护' /usr/local/bin/siyi_gimbal_relay.py     # 应为 5

# 3) 监听正常
ss -ulnp | grep 37260

# 4) 日志能看到转发与回传
journalctl -u siyi-gimbal-relay -n 20 --no-pager
```

日志中每条指令都有 `-> 转发` 和 `<- 回传` 两行；只有 `->` 没有 `<-`
说明相机没应答（查网线/相机供电/是否误发了心跳）。

## 2. 视频中转（MediaMTX）

MediaMTX 从相机拉 RTSP 并对外提供只读转发，配置在 `/etc/mediamtx/mediamtx.yml`：

```yaml
paths:
  a8mini:
    source: rtsp://192.168.144.25:8554/main.264
    sourceOnDemand: true          # 无人观看时不拉流，保护相机
    rtspTransports: [tcp]
```

认证与网段白名单：

```yaml
authInternalUsers:
  - user: a8viewer                # 只读账号，口令不要写进代码仓库
    pass: <在此填写口令>
    permissions:
      - action: read
        path: a8mini
  - action: publish
    ips: [127.0.0.1]
```

> 路径名叫 `main.264`，但实际是 **H.265** 编码（相机固定这样命名），
> 所以地面站用 `decodebin` 自动识别编码，不要写死解码器。

地面站侧在「系统设置 → 认证账号」填写该只读账号的口令，
它会保存在本机配置（`~/.config/.../VideoStream.conf`），不进仓库。

## 3. 网卡与地址

| 接口 | 地址 | 用途 |
| --- | --- | --- |
| `rtl8168` | `192.168.144.30/24` | 接相机 |
| `wlan0` | `192.168.2.113/24` | 接地面站（Wi-Fi） |

相机固定 `192.168.144.25`。若地面站电脑同时有有线和无线，
注意**路由不要走错网卡**（`ip route get 192.168.144.25` 确认）。

## 4. 快速验证（在机载电脑上执行）

```bash
# 视频：抓 10 帧不报错即通
gst-launch-1.0 -q rtspsrc location=rtsp://192.168.144.25:8554/main.264 \
  protocols=tcp ! fakesink num-buffers=10 && echo "视频 OK"

# 控制：读云台姿态（只读，安全）
python3 - <<'EOF'
import binascii, socket, struct
def crc16(d): return binascii.crc_hqx(d, 0)
body = b"\x55\x66\x01" + struct.pack("<H",0) + struct.pack("<H",0) + b"\x0d"
pkt = body + struct.pack("<H", crc16(body))
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(2.5)
s.sendto(pkt, ("192.168.144.25", 37260))
d, _ = s.recvfrom(2048)
y, p, r = struct.unpack_from("<hhh", d, 8)
print("yaw=%.1f pitch=%.1f roll=%.1f" % (y/10, p/10, r/10))
EOF
```

> ⚠️ 上面这段**只读**探测刻意不发任何控制指令，也**绝不发 0x00 心跳**
> （UDP 不支持，会让相机挂死）。恢复方法见项目根目录 `GIMBAL_CONTROL.md`
> 的软重启章节（`0x80` + 等 20~40 秒）。
