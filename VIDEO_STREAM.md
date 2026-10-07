# 实时视频流接入说明

地面站（`agri_gcs_visual`）已接入无人机云台相机的 **RTSP 实时图传**。本文说明
地址依据、实现结构、使用步骤与故障排查。

## 0. 实测可用的部署拓扑（2026-10-07 现场验证）

```text
A8 mini 云台相机                      Jetson 机载电脑 (ubuntu / 192.168.2.113)
  192.168.144.25:8554/main.264   ──►  网卡 rtl8168  192.168.144.30/24
  （H.265 编码，TCP）                   │
                                        ├─ MediaMTX v1.21.1 中转服务
                                        │    /etc/mediamtx/mediamtx.yml
                                        │    监听 0.0.0.0:8554，仅 TCP
                                        │    路径 a8mini，按需拉流(sourceOnDemand)
                                        │    只读账号 a8viewer，仅限本地网段
                                        │
  地面站（笔记本上的虚拟机）  ◄──────────┘  经 Wi-Fi 192.168.2.113:8554
  rtsp://192.168.2.113:8554/a8mini
  认证 a8viewer / <见 MediaMTX 配置>
```

**地面站默认就用这条链路**（设置项第一项「Jetson 中转 · a8mini（实测可用）」）。

为什么不是直连相机：地面站所在的笔记本只有 Wi-Fi，没有到 `192.168.144.0/24`
的路由，**无论如何都 ping 不到相机**。相机只挂在机载电脑的 `rtl8168` 网卡上，
因此必须先由机载电脑（MediaMTX）中转出来。

MediaMTX 的关键配置（`/etc/mediamtx/mediamtx.yml`）：

```yaml
authMethod: internal
authInternalUsers:
  - user: a8viewer
    pass: <16 位随机串>
    ips: [127.0.0.1, 192.168.1.0/24, 192.168.2.0/24]   # 只允许本地网段读
    permissions: [{ action: read, path: a8mini }]
rtspAddress: 0.0.0.0:8554
rtspTransports: [tcp]          # 只开 TCP，禁用 UDP
paths:
  a8mini:
    source: rtsp://192.168.144.25:8554/main.264
    rtspTransport: tcp
    sourceOnDemand: true       # 无人观看时不占相机的连接
```

配置注释原文也确认了编码事实：*"No transcoding. The camera path is named
main.264 but carries H.265."*

排障入口：

```bash
# 在机载电脑上
systemctl status mediamtx-a8mini
journalctl -u mediamtx-a8mini -n 50        # 能看到连接与认证失败记录
```

## 1. 地址与协议依据

### 1.1 手册地址表（《A8 mini 云台相机用户手册》2.3 节）

| 设备 | RTSP 地址 |
| --- | --- |
| 思翼 AI 相机 | `rtsp://192.168.144.60:554/video0` |
| 吊舱/云台相机 主码流 | `rtsp://192.168.144.25:8554/video1` |
| 吊舱/云台相机 副码流 | `rtsp://192.168.144.25:8554/video2` |
| 三防摄像头 A 款 / 天空端 HDMI 模块 | `rtsp://192.168.144.25:8554/main.264` |

手册 2.3.2 明确：**同一个 RTSP 地址最多支持 4 路同时拉流**，因此地面站拉流
不会挤掉机载电脑上的识别程序。

### 1.2 实机记录

`diff-planner-A8mini-master/A8mini/README.md` 记录 A8 mini 属于手册中的
**旧地址机型**，实机用的是 `main.264`。并且 `a8mini_detection.yaml` 特别注明：

> 真机流是 H.265，不能根据 main.264 文件名判断；GStreamer 后端需要此参数。

因此地面站**不把 `main.264` 当作 H.264**，默认交给 `decodebin` 自动识别
解复用器与解码器，同时保留手动强制 H.264/H.265 的选项。

## 2. 实现结构

| 文件 | 作用 |
| --- | --- |
| `qml/components/VideoConfig.qml` | 全局单例：地址预设、编码/传输/缓冲配置、持久化、GStreamer 流水线构造 |
| `qml/components/VideoStream.qml` | 播放器：MediaPlayer + VideoOutput，负责连接、状态机、重连、看门狗 |
| `qml/components/VideoPanel.qml` | 面板：真实画面 + 占位图 + 状态角标 + 热成像色调叠层 |
| `qml/pages/SettingsPage.qml` | 系统设置 → 连接设置 → 网络与通信 中的视频流配置区 |
| `src/main.cpp` | 新增 `--screenshot-delay <ms>`，便于自动化截图验证 |

配置通过 `Qt.labs.Settings`（category `VideoStream`）持久化，重启后保留。

### 2.1 流水线

首选自定义流水线（`gst-pipeline:` 前缀），这样能强制 TCP 并控制缓冲：

```
rtspsrc location="rtsp://192.168.144.25:8554/main.264"
        protocols=tcp latency=100 drop-on-latency=true tcp-timeout=5000000
  ! decodebin            # 自动识别 H.264/H.265
  ! videoconvert
  ! qtvideosink          # Qt 的视频接收端，接 VideoOutput
```

手动指定编码时改为 `rtph264depay ! h264parse ! avdec_h264` 或
`rtph265depay ! h265parse ! avdec_h265`。

**为什么要用 TCP**：手册更新记录里两次修复「RTSP 拉流花屏」，机载项目也统一
用 `protocols=tcp`；UDP 在丢包时会出现花屏/马赛克。

### 2.2 健壮性

- **回退**：自定义流水线起不来时自动改用普通 `rtsp://` 地址，交给 Qt 的
  playbin 处理，保证"至少能出画面"；两种方式交替重试。
- **退避重连**：失败按 1s → 2s → 4s → 8s 退避重试，界面不阻塞。
- **不报假在线**：实测发现 Qt 的 playbin 在 RTSP 连不上时也可能短暂上报
  `Buffered + Playing`（界面显示"实时"却是黑屏）。实验对照确认
  `hasVideo` / `bufferStatus` 在该路径下取不到有效值，**唯一可靠的判据是
  `position` 是否持续推进**。因此"实时"必须同时满足
  「status=Buffered/Buffering」+「playbackState=Playing」+「position 在最近
  2.5 秒内推进过」；另有 2 秒周期的推进看门狗，一旦声称在播但 position
  冻结就强制重连并回到"重连中"。用 `--video-diag` 启动可每秒打印
  status/state/position/驱动判定，便于现场排查。
- **看门狗**：出现"已连接但停在 Loaded 不播放"（窗口尚未暴露时
  qtvideosink 挂不上）会在 4 秒后强制重启。所有启动请求都经过 600ms 合并
  延时，等窗口就绪后再建连接。
- **单路解码**：只有大面板拉流；`clean: true` 的小缩略图保持静态占位，
  避免同一路相机被软件解码多次白吃 CPU。
- **断流可见**：面板角标显示 未接入/连接中/图传实时/重连中（第 N 次），
  设置页「图传状态」与右侧系统状态表同步显示；失败时点击画面可立即重连。

## 3. 使用步骤

### 3.1 依赖（Ubuntu 22.04）

```bash
sudo apt-get install -y qml-module-qtmultimedia libqt5multimedia5-plugins \
  gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
  gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-rtsp
```

`gstreamer1.0-libav` 提供软件 H.265 解码（`avdec_h265`），x86 机器上必需；
Jetson 上可换成 `nvv4l2decoder` 进一步降低 CPU 占用。

### 3.2 网络准备

**推荐（实测可用）**：地面站能访问机载电脑的 Wi-Fi 地址即可，默认预设
「Jetson 中转 · a8mini」直接可用；认证账号在设置页「认证账号」两格里
（默认 `a8viewer` + MediaMTX 配置里的口令）。中转账号只有 `a8mini` 路径的
只读权限，且限定本地网段。

**若要直连相机**（仅当地面站所在机器有网卡在 `192.168.144.0/24` 时可行）：

```bash
sudo ip addr add 192.168.144.30/24 dev <接相机的网卡>
ping 192.168.144.25
```

此时把预设切到「相机直连 · main.264」，并把认证账号清空。

### 3.3 界面操作

系统设置 → 连接设置 → 网络与通信：

1. **视频流地址**：选择 `A8 mini 主码流 · main.264`（实机默认）、
   `吊舱主码流 · video1`、`吊舱副码流 · video2`，或选「自定义地址」后
   在下一行填写；
2. **传输 / 编码**：默认 `TCP` + `自动识别`，一般不用改；
3. **图传状态**：显示实时状态，右侧数字是缓冲（ms），点「重连」立即重连；
4. 右侧「实时视频」面板出现画面即成功，左上角角标变绿显示「图传实时」。

配置改动会立即重建连接，无需重启程序。

## 4. 故障排查

| 现象 | 排查方向 |
| --- | --- |
| 角标一直「连接中」 | `ping 192.168.144.25`；确认网卡地址在同网段；确认相机已上电完成初始化 |
| 角标「重连中（第 N 次）」 | 相机未推流或地址写错；用 `gst-launch-1.0 rtspsrc location=<地址> protocols=tcp ! fakesink` 单独验证 |
| 有画面但花屏 | 确认传输方式是 TCP；把缓冲 latency 从 100 调到 200~300 试 |
| 画面黑但显示「图传实时」 | 理论上已被 position 判据挡住；若仍出现，用 `--video-diag` 启动看 position 是否真的在涨 |
| 提示缺少解码器 | 安装 `gstreamer1.0-libav`（H.265）；Jetson 上确认 `nvv4l2decoder` 可用 |

## 5. 无实机自测

仓库外可用 GStreamer 起一路本地 RTSP 测试流（把 H.265 挂在 `/main.264`
上，精确复现"文件名是 .264、内容是 H.265"的实机情况），再让地面站拉流截图：

```bash
# 终端 1：本地测试流（需要 python3-gi 与 gir1.2-gst-rtsp-server-1.0）
python3 rtsp_test_server.py 8554

# 让 192.168.144.25 指向本机，即可用生产地址直接测试
sudo ip addr add 192.168.144.25/24 dev lo

# 终端 2：离屏渲染截图验证
./build/agri_gcs_visual --software --screenshot /tmp/shot.png \
  --screenshot-delay 7000 --size 1536x1024
```

> ⚠️ **测试结束后必须删除环回别名**：
> `sudo ip addr del 192.168.144.25/24 dev lo`
> 否则本机路由会优先把 `192.168.144.25` 解析到环回地址，接真实相机时会出现
> "能 ping 通但拉不到流"的假连通现象。

注意：`--screenshot` 走 `grabToImage`，**抓不到 GStreamer 视频纹理**（视频区会是
黑色），这只影响离屏截图；界面里正常显示。要连画面一起验证，请在图形会话中
直接运行程序后截屏。

## 6. 尚未实现

- 云台的 UDP 控制协议（`192.168.144.25:37260`，拍照/录像/角度）未接入本地面站；
  当前只有机载 ROS 侧的 `a8mini_gimbal_node.py` 在使用；
- 「热成像」按钮仍是界面色调演示，没有第二路热成像视频源；
- 拉流分辨率切换（手册 5.3：720p/1080p）需要走相机 SDK，尚未接入。
