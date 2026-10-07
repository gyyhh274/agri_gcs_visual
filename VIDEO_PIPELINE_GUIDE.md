# 云台图传链路打通指南

> 记录 2026-10-07 把 A8 mini 云台相机的实时画面接进地面站的完整过程。
> 目的不是"记流水账"，而是把**排查思路、判读依据、每条命令为什么这么写**留下来，
> 方便后续换机器、换相机、换网络时照着复现。
>
> 配套文档：[VIDEO_STREAM.md](VIDEO_STREAM.md)（地面站视频功能的实现细节）

---

## 1. 最终打通的链路

```text
┌──────────────────────────────────────────────────────────────┐
│ A8 mini 云台相机                                              │
│   RTSP: rtsp://192.168.144.25:8554/main.264                  │
│   编码: H.265（注意：路径名叫 main.264，但不是 H.264！）        │
└───────────────────────┬──────────────────────────────────────┘
                        │ 网线（相机自带网口）
                        ▼
┌──────────────────────────────────────────────────────────────┐
│ Jetson 机载电脑（hostname: ubuntu，Ubuntu 20.04.6，L4T 5.10） │
│                                                              │
│   网卡 rtl8168   192.168.144.30/24   ← 与相机同网段           │
│   网卡 wlan0     192.168.2.113/24    ← 与地面站同一个 Wi-Fi   │
│                                                              │
│   MediaMTX v1.21.1（systemd 服务: mediamtx-a8mini）           │
│     /etc/mediamtx/mediamtx.yml                               │
│     监听 0.0.0.0:8554，rtspTransports: [tcp]（只有 TCP）      │
│     路径 /a8mini → source rtsp://192.168.144.25:8554/main.264│
│     只读账号 a8viewer，IP 白名单 127.0.0.1/1.0/24/2.0/24      │
│     sourceOnDemand: true（没人看时不占用相机连接）             │
└───────────────────────┬──────────────────────────────────────┘
                        │ Wi-Fi
                        ▼
┌──────────────────────────────────────────────────────────────┐
│ 地面站（笔记本 192.168.2.107 上的 Ubuntu 22.04 虚拟机）        │
│   rtsp://192.168.2.113:8554/a8mini                           │
│   认证 a8viewer / <MediaMTX 配置里的口令>                     │
│                                                              │
│   GStreamer 流水线（地面站内部）：                             │
│     rtspsrc location=... protocols=tcp latency=200            │
│             user-id=a8viewer user-pw=...                      │
│       ! decodebin        ← 自动识别 H.264/H.265               │
│       ! videoconvert                                         │
│       ! qtvideosink      ← 接 Qt Quick 的 VideoOutput         │
└──────────────────────────────────────────────────────────────┘
```

**一句话总结**：相机只挂在 Jetson 的网卡上，地面站所在机器没有到
`192.168.144.0/24` 的路由，所以必须先由 Jetson 上的 MediaMTX 把流转发到
Wi-Fi 网段，地面站再从这个中转地址拉流。

---

## 2. 故障现象与根因

**现象**：地面站「实时视频」面板一直黑屏 / 显示"重连中"，拉不到任何流。

**根因**：**不是代码问题，是网络路径问题。**

| 探测对象 | 结果 | 说明 |
| --- | --- | --- |
| 地面站机器 → `192.168.144.25`（相机） | ❌ ping 超时 | 笔记本只有 Wi-Fi，没有 144 网段路由 |
| 地面站机器 → `192.168.2.113`（Jetson） | ✅ 通，22/8554 端口开放 | Wi-Fi 同网段 |
| Jetson → `192.168.144.25` | ✅ **0.2 ms** | 相机就在 Jetson 的 `rtl8168` 网线上 |

RTSP 是 TCP 连接，**必须先有 IP 可达性**。路由不通的情况下，无论怎么改
地面站代码或解码参数都不可能出画面。所以排查一定要**从网络层往上走**。

---

## 3. 排查方法论（可复用，按顺序做）

### 第 1 步：先判断"不通"是哪一层的问题

```powershell
# Windows：看去相机的包走哪块网卡
Find-NetRoute -RemoteIPAddress 192.168.144.25
Get-NetIPAddress -AddressFamily IPv4          # 本机有哪些网段

# 连通性
ping 192.168.144.25
Test-NetConnection 192.168.144.25 -Port 8554
```

**判读要点**：

- 路由显示走的是**默认网关**（而不是某块直连网卡）→ 说明本机根本不在相机
  网段，包会被扔给路由器然后丢弃。这**不是防火墙问题**，加防火墙规则没用。
- 区分 **RST（被拒绝）** 和 **超时（被丢弃）**：
  - 秒回 "Connection refused" → 有路由但**没有服务在监听**
  - 一直超时 → **路由不通或防火墙 DROP**
  这两者处理方式完全不同，别混。

### 第 2 步：找相机到底挂在哪台机器上

线索来源，从便宜到贵：

1. **项目文档里搜 IP**（最省事）
   ```powershell
   Get-ChildItem <项目> -Recurse -Include *.md,*.sh,*.yaml -File |
     Select-String -Pattern '\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}' -AllMatches |
     ForEach-Object { $_.Matches } | ForEach-Object { $_.Value } |
     Group-Object | Sort-Object Count -Descending | Select-Object -First 15
   ```
   当时就是这样找到 `机载电脑：192.168.1.50` 这条记录的。

2. **逐个探测候选地址**：ping + 22 端口。

3. **看 SSH banner 判断是什么系统**（不用登录）：
   ```
   SSH-2.0-OpenSSH_8.9p1 Ubuntu-3ubuntu0.17   → Ubuntu 22.04
   SSH-2.0-OpenSSH_8.2p1 Ubuntu-4ubuntu0.13   → Ubuntu 20.04
   ```
   用 `TcpClient` 连上去读第一行即可。

4. **其它工具留下的连接信息**：比如 NoMachine 的连接面板会直接写着
   host / port / 用户名 —— 当时就是用户截图里的 `192.168.2.113` 提供了关键线索。

### 第 3 步：登录目标机器，确认它和相机通

```bash
ip -4 -o addr show scope global     # 找哪块网卡在 192.168.144.x
ip route get 192.168.144.25         # 确认走那块网卡
ping -c 3 192.168.144.25            # 0.2ms → 相机就在这条网线上
```

### 第 4 步：查有没有现成的转发服务（关键一步，别急着自己写）

```bash
ss -tlnp | grep 8554
systemctl list-units --type=service --all | grep -iE 'rtsp|relay|cam|stream'
systemctl cat mediamtx-a8mini.service
```

当时就是这样发现了 `mediamtx-a8mini.service`（Description 写着
"A8 mini RTSP TCP proxy (MediaMTX)"），**避免了重复造轮子**。

### 第 5 步：识别 RTSP 服务类型和正确路径

**不依赖任何客户端库**，直接手写 RTSP 请求（这一步很实用，gst/ffmpeg 探不到
的时候手工发请求最可靠）：

```text
OPTIONS rtsp://192.168.2.113:8554/ RTSP/1.0
CSeq: 1

```

响应：

```text
RTSP/1.0 200 OK
Public: DESCRIBE, ANNOUNCE, SETUP, PLAY, RECORD, PAUSE, GET_PARAMETER, TEARDOWN
Server: gortsplib          ← MediaMTX 就是基于 gortsplib 的
```

然后**逐个探测路径**，判读规则很重要：

| 响应 | 含义 |
| --- | --- |
| `400 Bad Request` | 该路径不存在 |
| **`401 Unauthorized`** | **该路径存在，但需要认证** ← 这就是要找的 |
| `200 OK` + SDP | 路径存在且无需认证，可直接用 |

```text
DESCRIBE rtsp://192.168.2.113:8554/a8mini RTSP/1.0
CSeq: 1
Accept: application/sdp

```

→ `401` + `WWW-Authenticate: Basic realm="ipcam"`，于是确定：
**路径是 `/a8mini`，认证方式是 HTTP Basic。**

### 第 6 步：拿凭据

两条路：

1. **猜常见弱口令**（当时先试了 15 组，全 401）—— 说明不是默认口令。
2. **直接读服务配置**（正道）：
   ```bash
   sudo cat /etc/mediamtx/mediamtx.yml
   ```
   配置里 `authInternalUsers` 就是账号、口令、IP 白名单、权限，一目了然：
   ```yaml
   authInternalUsers:
     - user: a8viewer
       pass: <16 位随机串>
       ips: [127.0.0.1, 192.168.1.0/24, 192.168.2.0/24]
       permissions:
         - action: read
           path: a8mini
   ```
   顺带还能确认 `paths.a8mini.source` 指向相机真实地址、`rtspTransports: [tcp]`，
   以及一条重要注释：*"The camera path is named main.264 but carries H.265."*

### 第 7 步：**先用命令行验证链路，再去改代码**（最重要的习惯）

只要网络层能通，就用 `gst-launch-1.0` 单独验证，**不要一上来就编译整个地面站**：

```bash
# 最简连通 + 解码验证（退出码 0 就是通了）
timeout 40 gst-launch-1.0 -q \
  rtspsrc location="rtsp://a8viewer:<口令>@192.168.2.113:8554/a8mini" \
          protocols=tcp latency=200 ! \
  decodebin ! videoconvert ! fakesink num-buffers=45
echo "退出码=$?"

# 抓真实画面存成 PNG（眼见为实）
timeout 30 gst-launch-1.0 -q \
  rtspsrc location="rtsp://a8viewer:<口令>@192.168.2.113:8554/a8mini" \
          protocols=tcp latency=200 ! \
  rtph265depay ! h265parse ! avdec_h265 ! videoconvert ! \
  videorate ! video/x-raw,framerate=1/2 ! pngenc ! \
  multifilesink location=/tmp/cam%02d.png
ls -lh /tmp/cam*.png
```

> **踩过的坑**：`num-buffers` 是 **rtspsrc** 的属性，不是 `multifilesink` 的。
> 写错了会报 `no property "num-buffers" in element "multifilesink0"`。
> 抓帧时用 `timeout` 控制时长，别指望在 sink 上限制帧数。

### 第 8 步：最后才改地面站

见第 5 节。

---

## 4. 为什么不用别的方案

| 方案 | 是否可行 | 原因 |
| --- | --- | --- |
| 地面站直连相机 `192.168.144.25` | ❌ | 笔记本无 144 网段路由（除非给笔记本接一块网线到相机网段） |
| SSH 端口转发 `ssh -L 8554:192.168.144.25:8554` | ✅ 可行 | 但要长期挂着一条隧道，且没用到 Jetson 上已有的中转 |
| **用 Jetson 上现成的 MediaMTX 中转** | ✅ **采用** | 服务已经在跑（systemd 管理、开机自启、按需拉流），地面站只需填对地址和账号 |
| 在地面站电脑上再装一套 MediaMTX 转发 | ❌ | 多一跳、多一个要维护的进程，没必要 |

**MediaMTX 中转的好处**（也是它已经被部署在这里的原因）：

- `sourceOnDemand: true`：**没有客户端观看时断开与相机的连接**，
  不给相机增加常驻连接；有人看才拉。
- 相机侧只产生 1 路连接，多个地面站客户端共享 → 手册里"同一 RTSP 地址最多
  4 路"的限制不容易被撞到。
- 认证、IP 白名单、只读权限都在中转层做，相机本身不用改配置。
- 顺便还能提供 WebRTC/HLS（配置里现在是关掉的）给浏览器看。

---

## 5. 地面站侧需要改什么

改动集中在两处（详见 [VIDEO_STREAM.md](VIDEO_STREAM.md)）：

### 5.1 `qml/components/VideoConfig.qml` —— 地址、凭据、流水线

1. **预设地址**第一项改成中转地址，并设成默认：
   ```
   预设 0: rtsp://192.168.2.113:8554/a8mini        ← 默认
   预设 1: rtsp://192.168.144.25:8554/main.264     （仅当本机有 144 网段时可用）
   预设 2: rtsp://192.168.144.25:8554/video1
   预设 3: rtsp://192.168.144.25:8554/video2
   预设 4: 自定义地址
   ```
2. **认证字段**（持久化，设置页可改）：
   ```qml
   property string userId: "a8viewer"
   property string userPw: "<MediaMTX 里的口令>"
   ```
3. **流水线里带上认证** —— 属性名是 `user-id` / `user-pw`：
   ```
   rtspsrc location="..." protocols=tcp latency=200
           user-id="a8viewer" user-pw="..."
     ! decodebin ! videoconvert ! qtvideosink
   ```

### 5.2 `qml/pages/SettingsPage.qml` —— 让用户能改

在「网络与通信」卡片里加一行「认证账号」：用户名输入框 + 密码输入框
（密码用 `echoMode: TextInput.Password`）。

### 5.3 三个必须记住的实现要点

| 要点 | 原因 |
| --- | --- |
| 用 `decodebin` 自动识别编码 | 源路径叫 `main.264`，实际是 H.265。强行按 H.264 解会直接失败 |
| 必须 `protocols=tcp` | MediaMTX 配置里 `rtspTransports: [tcp]`，UDP 根本协商不上 |
| **"实时"要看 `position` 是否推进** | Qt 的 playbin 在连不上时也会短暂上报 `Buffered + Playing`，只看状态会**假在线**（详见 VIDEO_STREAM.md 2.2 节） |

---

## 6. 换一套环境时怎么复现（检查清单）

- [ ] 1. 相机上电，网线接到**哪台机器**？在那台机器上 `ping <相机IP>` 必须通
- [ ] 2. 那台机器有没有第二条网络能到地面站（Wi-Fi/交换机）？
- [ ] 3. 那台机器上有没有现成的 RTSP 转发服务？
      `systemctl list-units | grep -i rtsp`、`ss -tlnp | grep 8554`
- [ ] 4. 有服务 → 用 `OPTIONS` 确认类型、用 `DESCRIBE` 逐路径探测，
      **`401` 就是正确路径**
- [ ] 5. 读服务配置拿账号/口令/白名单（MediaMTX: `authInternalUsers`）
- [ ] 6. 在**地面站机器**上用 `gst-launch` 拉流验证（先 `fakesink` 看退出码，
      再 `pngenc` 抓帧眼见为实）
- [ ] 7. 图片真的抓到 → 才去改地面站的地址、凭据、流水线
- [ ] 8. 改完编译，在地面站里确认角标变「图传实时」且画面在动
- [ ] 9. 跑一遍回归测试：`ctest --test-dir build-tests`
- [ ] 10. 记录到本文档的"环境信息"一节

---

## 7. 故障对照表

| 现象 | 最可能的原因 | 怎么确认 | 处置 |
| --- | --- | --- | --- |
| `ping 相机` 不通 | 本机不在相机网段 | `ip route get <相机IP>` 看是否走默认网关 | 换到有路由的机器，或经中转 |
| 8554 端口超时 | 路由不通 / 防火墙 DROP | 对比同网段其它端口 | 先解决网络 |
| 8554 立即拒绝 | 服务没起 | `systemctl status mediamtx-a8mini` | 启动服务 |
| `401 Unauthorized` | 账号口令不对或来源 IP 不在白名单 | 看响应头 `WWW-Authenticate`；查配置 `ips:` | 用正确账号；注意客户端出口 IP |
| `400 Bad Request` | 路径写错 | 用 `OPTIONS` 看服务类型，再逐路径 `DESCRIBE` | 改成正确路径（本例 `/a8mini`） |
| 有 SDP 但解码失败 | 编码判断错（把 H.265 当 H.264） | `gst-launch ... ! decodebin ...` 会自动识别 | 去掉强制的 depay/parse，改用 `decodebin` |
| 能连上但画面不动 | UDP 丢包 / 协商成 UDP | 日志里看 transport | 强制 `protocols=tcp` |
| 地面站角标「图传实时」但黑屏 | 假在线（Qt 报 Buffered 但没帧） | 用 `--video-diag` 看 `position` 是否推进 | 已由 position 判据 + 冻结看门狗处理 |
| 画面卡顿 | 缓冲太小 / Wi-Fi 抖动 | 改 latency 试 | 把 latency 从 100 调到 200~300 |
| MediaMTX 日志里大量 `authentication failed` | 客户端口令错或 IP 不在白名单 | `journalctl -u mediamtx-a8mini -n 50` | 修正账号；**这是最快的排障入口** |

---

## 8. 环境信息（本次实测）

| 项目 | 值 |
| --- | --- |
| 相机 | SIYI A8 mini，`192.168.144.25:8554/main.264`，**H.265** |
| 机载电脑 | Jetson，hostname `ubuntu`，Ubuntu 20.04.6 LTS，kernel `5.10.216-tegra` |
| 机载电脑网卡 | `rtl8168` = 192.168.144.30/24（接相机）、`wlan0` = 192.168.2.113/24（Wi-Fi） |
| 中转服务 | MediaMTX v1.21.1，`/usr/local/lib/mediamtx/v1.21.1/mediamtx`，配置 `/etc/mediamtx/mediamtx.yml` |
| 中转地址 | `rtsp://192.168.2.113:8554/a8mini`（只读账号 `a8viewer`，仅 TCP） |
| 地面站 | Ubuntu 22.04.5 虚拟机，Qt 5.15.3 + GStreamer 1.20.3 |
| 验证结果 | `decodebin` 解码 45 帧退出码 0；抓到 1920×1080 真实帧；地面站角标「图传实时」且画面在动 |

---

## 9. 常用命令速查

```bash
# ── 在机载电脑上 ──────────────────────────────────────────────
systemctl status mediamtx-a8mini                 # 中转服务状态
journalctl -u mediamtx-a8mini -n 50              # 中转日志（排障首选）
sudo cat /etc/mediamtx/mediamtx.yml              # 路径/账号/白名单
ss -tlnp | grep 8554                             # 谁在监听
ip -4 -o addr show scope global                  # 本机网段
ping -c 3 192.168.144.25                         # 相机连通性

# ── 在地面站机器上 ────────────────────────────────────────────
# 1) 端口可达性（不依赖任何工具）
timeout 5 bash -c 'cat < /dev/null > /dev/tcp/192.168.2.113/8554' && echo OK

# 2) 解码验证（退出码 0 = 通）
timeout 40 gst-launch-1.0 -q \
  rtspsrc location="rtsp://a8viewer:<口令>@192.168.2.113:8554/a8mini" \
          protocols=tcp latency=200 ! \
  decodebin ! videoconvert ! fakesink num-buffers=45; echo "退出码=$?"

# 3) 抓真实画面
timeout 30 gst-launch-1.0 -q \
  rtspsrc location="rtsp://a8viewer:<口令>@192.168.2.113:8554/a8mini" \
          protocols=tcp latency=200 ! \
  rtph265depay ! h265parse ! avdec_h265 ! videoconvert ! \
  videorate ! video/x-raw,framerate=1/2 ! pngenc ! \
  multifilesink location=/tmp/cam%02d.png

# 4) 地面站带诊断输出启动（每秒打印 status/position，判断是否假在线）
./build/agri_gcs_visual --video-diag

# 5) 离屏截图快速回归（注意：抓不到视频纹理，只验证 UI）
./build/agri_gcs_visual --software --screenshot /tmp/s.png \
  --screenshot-delay 8000 --size 1536x1024
```

```powershell
# ── 在 Windows 上用原始 RTSP 请求探服务（不需要任何客户端库）────
# OPTIONS 看服务类型；DESCRIBE 探路径：400=不存在，401=存在需认证，200=可直接用
```
