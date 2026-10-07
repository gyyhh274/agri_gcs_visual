# 云台实时操控说明

> A8 mini 云台的实时控制实现与踩坑记录。配合
> [云台图传链路打通指南](VIDEO_PIPELINE_GUIDE.md)（视频）阅读。

---

## 1. 链路拓扑

```text
地面站（虚拟机）                 Jetson 机载电脑 192.168.2.113          相机
  GimbalLink (QUdpSocket)  ──►  siyi-gimbal-relay (UDP 37260)  ──►  192.168.144.25:37260
       UDP → 192.168.2.113:37260        单一持久上游 socket              （SIYI 以太网 SDK）
```

相机控制口只对机载电脑可达，所以在 Jetson 上跑一个 UDP 中转
（systemd 服务 `siyi-gimbal-relay`，脚本 `/usr/local/bin/siyi_gimbal_relay.py`）。

```bash
# 机载电脑上查看/管理
systemctl status siyi-gimbal-relay
journalctl -u siyi-gimbal-relay -n 30      # 每条转发/回传都有日志，排障首选
sudo systemctl restart siyi-gimbal-relay
```

---

## 2. ⚠️ 两个必须知道的约束（都是实测踩出来的）

### 2.1 心跳包 CMD 0x00 只能走 TCP，对 UDP 发会让相机挂死

SDK 文档写得很清楚：`0x00：TCP心跳` / `心跳包：55 66 01 01 00 00 00 00 00 59 8B` /
**"仅TCP连接时支持"**。

实测后果：向 UDP 37260 发送心跳帧后，相机的命令服务**完全停止响应**
（UDP 无应答，TCP 37260 也从"可连接"变成"拒绝连接"），视频 RTSP 和 ping 仍正常。
持续约 **9 分钟**无响应。

**处置**：

* 地面站客户端**不实现心跳**（`GimbalLink` 里根本没有 0x00 这条路）。
* 中转服务**直接丢弃所有 CMD 0x00 帧**，防止其他工具误发。
* 若不慎挂死，按下面的恢复流程处理。

### 2.2 恢复流程：SDK 软重启 0x80（不需要断电）

```text
发送: 55 66 01 02 00 00 00 80 01 01 <crc>      # 载荷 [相机重启=1, 云台复位=1]
```

* **该命令不回 ACK**，不要因为没应答就以为失败。
* 实测：发送后约 **20~40 秒**控制通道恢复；复位后云台角度会被重置
  （观测到 yaw 从 135° 变为 7.3°），属正常现象。
* 若软重启无效，只能给相机断电重上电。

地面站里对应「云台控制」面板上的 **软重启** 按钮。

### 2.3 A8 mini 的 UDP SDK 是单客户端模型

相机把会话绑在**一个源端口**上。中转 v1 曾为每个下游客户端新建一条上游 socket，
源端口不断变化，导致相机应答错乱。**v2 改为全局唯一的上游 socket**，源端口恒定。

地面站侧同理：`GimbalLink` 的 `QUdpSocket` 在整个生命周期内只 `bind` 一次，
不重连、不变更源端口。

---

## 3. 协议速查（SIYI Gimbal Camera External SDK V0.1.1）

### 3.1 帧格式

```text
55 66 | control | payload_len(2, LE) | seq(2, LE) | cmd(1) | payload | crc16(2, LE)
```

* `control`：`0x01` = 需要应答，`0x00` = 不需要
* **CRC16 = CCITT，多项式 0x1021，初值 0**（等价 Python `binascii.crc_hqx(data, 0)`）
* 校验范围：从帧头到 payload 结束（不含 CRC 本身）

### 3.2 本地面站已实现的命令

| 功能 | 命令 | 载荷 | 应答 | UI 入口 |
| --- | --- | --- | --- | --- |
| 获取姿态 | `0x0D` | 无 | `int16 yaw,pitch,roll,yawV,pitchV,rollV`（0.1°） | 面板实时角度 |
| 设置姿态 | `0x0E` | `int16 yaw, pitch`（0.1°） | `int16 yaw, pitch, roll`（实际值） | 俯仰/航向滑条、方向盘 |
| 系统信息 | `0x0A` | 无 | 字节[3]=录像状态（0停 1录 2无卡 3丢数据） | 录像状态显示 |
| 当前云台模式 | `0x19` | 无 | 1 字节：0锁定 1跟随 2FPV | 模式按钮高亮 |
| 拍照 | `0x0C` | `00` | **无 ACK** | 拍照 |
| 录像开关 | `0x0C` | `02` | **无 ACK**（切换式，需回查 0x0A 确认） | 录像/停止 |
| 云台模式 | `0x0C` | `03`锁定 / `04`跟随 / `05`FPV | **无 ACK** | 锁定/跟随/FPV |
| 一键朝下 | `0x0C` | `09` | **无 ACK** | 朝下 |
| 变倍步进 | `0x05` | `01` 拉近 / `FF` 拉远 | 无 | 变焦 +/− |
| **云台转向（拨杆）** | `0x07` | `int8 yaw速度, int8 pitch速度`（−100~100） | 1 字节 | **360° 拨杆** |
| 绝对变倍 | `0x0F` | `[整数, 小数×10]` 如 4.5x = `04 05` | 1 字节 | 变焦滑条 |
| 当前/最大变倍 | `0x18` / `0x16` | 无 | `[整数, 小数×10]` | 变焦显示与量程 |
| 一键回中 | `0x08` | `01` | 无 | 方向盘中心「回中」 |
| 软重启 | `0x80` | `[相机, 云台]` | **无 ACK** | 软重启 |

### 3.3 A8 mini 的能力边界（来自 SDK 文档）

| 项目 | 范围 |
| --- | --- |
| yaw | **−135.0° ~ +135.0°** |
| pitch | **−90.0° ~ +25.0°** |
| 变倍 | 1.0x ~ **3.5x**（实机 `0x16` 回报 `03 05`） |

> 注意：原界面把航向写成 ±180°、变焦写成 1~10x，都超出 A8 mini 实际能力，
> 现已按相机回报值动态取量程（`gimbalLink.yawLimit` / `pitchMin` / `pitchMax` / `zoomMax`）。

---

## 4. 360° 拨杆（速度控制）

面板左侧的圆形区域是**遥控器式拨杆**：按住拖动，云台按杆量对应的**速度**转动，
松手自动回中并停止。

### 4.1 为什么用速度而不是角度

`0x07` 的设计就是给拨杆用的（SDK 原文：*"滑动越长，数值越大，转向速度越大，
松手后发送 0，停止转向"*）：

```text
载荷 = int8 turn_yaw, int8 turn_pitch     范围 -100 ~ 0 ~ 100
```

| 杆量 | 效果 |
| --- | --- |
| 偏右 → 右 | 向右转，越靠边越快 |
| 偏上 → 上 | 向上转（pitch 增大） |
| 松手 | 发 `(0, 0)` 停止 |

实测速度：杆量 60 约 30°/s，满杆约 50~80°/s（含加减速）。

### 4.2 ⚠️ 方向约定：指令侧和回报侧是两套坐标，别搞混

这台云台有两套独立的符号约定，**很容易误判成"文档写错了"**：

| 侧 | 命令 | 约定 |
| --- | --- | --- |
| **指令侧** | `0x07 turn_yaw` | **正值 = 相机向右转**（顺时针俯视），**与 SDK 文档一致** ✅ |
| **回报侧** | `0x0D` 的 yaw 角度 | **正值 = 逆时针（向左）** |

所以会出现这个看着"反常"的现象：

```text
发 turn_yaw = +60（向右转）  →  回报 yaw 角度【变小】（如 66.4° → 11.7°）
```

**这是正常的**，两套约定方向相反而已，代码里**不要对 yaw 取反**。

**判定方向的唯一真相是画面**：相机向右转时，画面内容**向左**移动。
只看回报角度会得出相反结论 —— 这个坑踩过一次，见
[开发过程记录](GIMBAL_DEV_PROCESS.md) §12.2。

`0x07` 的 pitch 则与文档一致（正值 = 向上 = 抬头），且回报侧 pitch 正值也是向上，两侧同号。

### 4.3 三层安全防护（转向是流式指令，必须显式停止）

**风险**：`0x07` 发出后相机会一直转，直到收到 `(0,0)`。
如果地面站崩溃、断网、或鼠标抬起事件丢失，云台会持续转动到限位。

| 层 | 位置 | 机制 |
| --- | --- | --- |
| 1 | QML | 丢失鼠标抬起事件时，靠 `MouseArea.pressed` 真实状态强制回中（300 ms 轮询） |
| 2 | QML | 窗口失去激活（切走/最小化）立即回中 |
| 3 | C++ | 500 ms 收不到新杆量 → 自动发 `(0,0)`（死手保护） |
| 4 | **中转服务** | **1 秒收不到任何客户端的 `0x07` → 代替地面站向相机发送停止** |

第 4 层是最关键的：它覆盖地面站进程崩溃、网络中断等**客户端完全消失**的情况。
触发时中转日志会打印：

```text
!! 拨杆死手保护触发：1.0 秒无转向指令，已代替地面站发送停止
```

### 4.4 数据流

```text
拨杆拖动 → 20 Hz 持续上报杆量（不是只在移动时发）
         → GimbalLink::setRotateRate() 合并到约 22 Hz
         → 0x07（yaw 取反）→ 中转 → 相机
松手     → stopRotate() 发 (0,0)
```

> 为什么要 20 Hz 持续上报：一是相机不会自己保持速度，需要流式喂；
> 二是这样中转的死手保护才知道"地面站还活着"。

---

## 5. 地面站侧实现

### 4.1 C++：`src/GimbalLink.{h,cpp}`

以 QML 上下文属性 `gimbalLink` 暴露（与 `groundLink` 同样的用法）。

```qml
gimbalLink.connected          // 是否收到过应答（3 秒无姿态回报判为离线）
gimbalLink.status             // 文字状态
gimbalLink.yaw / pitch / roll // 相机回报的真实角度
gimbalLink.attitudeFresh      // 姿态是否新鲜（用于置灰显示）
gimbalLink.mode / modeName    // 0锁定 1跟随 2FPV
gimbalLink.recordStatus / recordStatusName
gimbalLink.zoom / zoomMax
gimbalLink.yawLimit / pitchMin / pitchMax   // 量程常量

gimbalLink.connectToGimbal(host, port)
gimbalLink.setAttitude(yaw, pitch)   // 绝对角度
gimbalLink.jog(dYaw, dPitch)         // 相对步进
gimbalLink.setRotateRate(yawRate, pitchRate)  // 拨杆速度 -100~100（右/上为正）
gimbalLink.stopRotate()              // 发 (0,0) 停止转向
gimbalLink.yawRate / pitchRate / rotating     // 当前杆量状态
gimbalLink.centerGimbal() / lookDown()
gimbalLink.setMode(0|1|2)
gimbalLink.takePhoto() / toggleRecording()
gimbalLink.zoomStep(+1|-1) / setZoomAbsolute(x)
gimbalLink.refreshStatus() / refreshAttitude() / softReboot()
```

关键实现细节：

* **无 ACK 的命令绝不等应答**。`0x0C` 系列与 `0x08` 都是发完即返回，
  之后用 `0x0A`/`0x18` 回查真实状态。否则会误报"云台无应答"并把状态打成离线。
* **滑条去抖**：拖动滑条会高频触发 `setAttitude`，内部合并为约 **12 Hz** 下发。
* **姿态轮询** 1 秒一次；录像/模式/变倍低频查询（每 3~5 秒）。
* **应答匹配**：按命令号匹配，不依赖序号回显（部分固件不回显序号）。
* **超时重试**：需要应答的命令 900ms 超时、最多 3 次，之后才判离线。

### 4.2 QML 接线要点

* 地址**从视频流地址推导**（中转和 MediaMTX 在同一台机器上）：
  ```qml
  readonly property string gimbalHost: {
      var m = /^rtsp:\/\/(?:[^@\/]*@)?([^:\/]+)/.exec(VideoConfig.url)
      return m ? m[1] : "192.168.2.113"
  }
  readonly property int gimbalPort: 37260
  ```
* 滑条双向同步：相机回报新姿态时刷新滑条，但**用户正在拖动时不打扰**
  （判断 `slider.pressed`）。

### 4.3 自检模式（现场排障用）

```bash
# 下发 pitch −8° 再复原，打印每一步的真实角度，验证完整回路
./build/agri_gcs_visual --gimbal-test
./build/agri_gcs_visual --gimbal-test --gimbal-host 192.168.2.113 --gimbal-port 37260
```

输出示例：

```text
[gimbal-test] 连接后            connected=1 yaw=53.7 pitch=0.1 zoom=1.0 mode=锁定 record=未录像
[gimbal-test] pitch-8 回读       connected=1 yaw=53.9 pitch=-7.8 ...   ← 云台真的动了
[gimbal-test] 复原回读         connected=1 yaw=54.1 pitch=0.1  ...   ← 精确复原
```

---

## 5. 面板功能对照

| 控件 | 行为 |
| --- | --- |
| **圆形拨杆** | 按住拖动 = `setRotateRate()`，360° 任意方向，杆量=速度；松手回中并停止 |
| 拨杆下方读数 | 实时杆量 `偏航 ±N 俯仰 ±N`（绿色=正在转向） |
| 俯仰角 / 航向角滑条 | `setAttitude()`，量程自动取相机能力（−90~25 / ±135） |
| 滑条两侧 −/+ | `jog()` 微调 1°（拨杆取代了原来的方向盘箭头） |
| 变焦滑条 | `setZoomAbsolute()`，量程 1.0~3.5x |
| 锁定 / 跟随 / FPV | `setMode()`，当前模式高亮 |
| 回中 | 一键回中（原拨杆中心按钮，已移到模式行） |
| 朝下 | 一键朝下（pitch 到 −90°） |
| 软重启 | `0x80`，命令服务挂死时的恢复手段 |
| 拍照 / 录像 / 变焦± | 对应 SDK 命令；录像按钮文案跟随真实录像状态 |
| 状态区 | 云台在线/离线 + 录像状态 + 当前模式 + 重连 |

---

## 6. 故障排查

| 现象 | 可能原因 | 确认方法 | 处置 |
| --- | --- | --- | --- |
| 拨杆推着不动 | 相机在限位（yaw ±135 / pitch −90~25） | 看面板角度是否已到边界 | 属正常限位；往反方向推 |
| 拨杆左右方向反了 | 被"回报角度"误导而加了取反 | 看**画面**移动方向，别只看角度 | 去掉 `flushRotate()` 里的取反（见 §4.2） |
| 松手后云台还在转 | 停止包丢失 / 地面站异常退出 | 中转日志有无「死手保护触发」 | 1 秒内会自动停；再点「回中」复位 |
| 云台转到限位停不下来 | 相机遇限位后不接受该方向速度 | 看角度读数 | 反向推杆或点「回中」 |
| 面板一直「云台离线」 | 中转未运行 / 地址不对 | 机载电脑上 `systemctl status siyi-gimbal-relay`；`ss -ulnp \| grep 37260` | 启动服务；在面板点「重连」 |
| 所有命令都无响应（UDP+TCP 都死） | **大概率误发了 0x00 心跳** | 看中转日志有没有 `已丢弃心跳帧`；`bash try_soft_reboot.sh` | 发 `0x80` 软重启，等 20~40 秒 |
| 只有拍照/录像"没反应" | 0x0C 系列本来就无 ACK | 回查 `0x0A` 录像状态 / 看 TF 卡 | 属正常；录像状态会自行刷新 |
| 云台不动但显示在线 | 姿态被限位钳制 | 看当前 yaw/pitch 是否已到 ±135 / −90~25 | 属正常限位 |
| 变倍值与设定值不一致 | 相机自身的变倍曲线/档位 | 回查 `0x18` | 以相机回报为准 |
| 中转日志刷 `已丢弃心跳帧` | 有别的工具在发 0x00 | `journalctl -u siyi-gimbal-relay` | 找出那个工具并停止 |

---

## 7. 常用命令速查

```bash
# ── 机载电脑 ────────────────────────────────────────────────
systemctl status siyi-gimbal-relay
journalctl -u siyi-gimbal-relay -n 30 --no-pager
sudo systemctl restart siyi-gimbal-relay

# ── 地面站机器：协议探测（只读）──────────────────────────────
python3 siyi_probe.py 192.168.2.113       # 查状态/姿态/模式/变倍
python3 verify_gimbal_commands.py         # 逐条验证拍照/录像/变倍/模式
                                          #（可逆操作会自动复原）

# ── 地面站应用 ──────────────────────────────────────────────
./build/agri_gcs_visual --gimbal-test     # 控制回路自检
```
