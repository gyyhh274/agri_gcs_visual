# 实时三维建图

地面站接收机载 Faster-LIO 已输出的世界坐标点云和位姿，累积显示三维环境、飞行轨迹与无人机姿态。右侧现有图传、云台控制和媒体功能继续使用各自链路。地图桥只订阅 ROS 数据，不启动 ROS master、雷达、建图、规划器、控制器或任何飞行指令。

数据链路：`MID-360 → Faster-LIO → pointcloud_bridge.py → SSH 本地端口转发 → MappingClient → PointCloudView`。

## 界面

- 任务页默认打开“实时建图”，可切回“离线地图”编辑原有航点。
- 左键拖动旋转，右键或 Shift + 左键平移，滚轮缩放，双击显示全图。
- 工具栏可选三维/俯视、跟随、全图、高度/反射强度着色、暂停接收、清空本地地图、导出 PCD。
- “连接”配置的是地图服务地址。默认 `127.0.0.1:37262`，由独立 SSH 隧道连接机载服务。
- 断线或点云过期时保留最后地图并显示提示；收到连接心跳或位姿不会把过期点云标为实时。
- “预览演示”需要明确确认，展示合成场景，并持续显示“演示数据”标记。不会因为机载离线而自动切换演示。

地图使用机载 `world` 局部坐标，单位米，未与离线卫星地图经纬度标定。高度着色是显示效果，不是相机真彩色贴图。当前方案复用 Faster-LIO，不额外增加回环或全局优化。

## 机载部署

模板对应用户 `ly`、ROS Noetic。将以下文件放在同一目录：

```text
pointcloud_bridge.py
agri-pointcloud-bridge.service
install_mapping_bridge.sh
```

以 `ly` 运行，安装过程中仅服务安装需要 sudo：

```bash
bash install_mapping_bridge.sh
systemctl status agri-pointcloud-bridge
journalctl -u agri-pointcloud-bridge -n 40 --no-pager
```

服务默认仅监听机载 `127.0.0.1:37262`，不直接暴露到 Wi-Fi。ROS 环境通过 service 中的 `PYTHONPATH` 和 `ROS_MASTER_URI` 指定；其他用户、ROS 版本或非本机 master 需调整 service。服务先启动 TCP，再在后台等待 ROS，即使机载建图没有启动，地面站也能看到“等待 ROS”状态。

默认只订阅：

| 话题 | 类型 | 用途 |
| --- | --- | --- |
| `/laserMapping/cloud_registered` | `sensor_msgs/PointCloud2` | 已转换到统一世界坐标的扫描点云 |
| `/laserMapping/odometry` | `nav_msgs/Odometry` | 同一坐标系下的位置和四元数姿态 |

点云与位姿坐标系不同时，桥丢弃不匹配的位姿，避免把不同坐标直接叠加。点云坐标系改变、采集时间倒退、发布者改变或 ROS 消息序号回退时，桥创建新 session 并清空旧地图；ROS master 中断后恢复也会重置一次。其他不会改变上述标记的人工定位重置仍需操作者显式清空，不能仅靠点云自动识别。

## 虚拟机隧道

在运行地面站的 Ubuntu 用户下安装：

```bash
bash install_mapping_tunnel.sh
```

脚本生成独立的 `~/.ssh/agri_mapping_ed25519` 密钥；私钥仅保存在本机，不放入仓库。经可信渠道核验机载 SSH 主机密钥后，以别名 `agri-onboard-pointcloud` 写入虚拟机用户的 `~/.ssh/known_hosts`。例如：

```text
agri-onboard-pointcloud ssh-ed25519 <已核验的机载主机公钥>
```

在机载用户 `~/.ssh/authorized_keys` 为该隧道公钥添加限制：

```text
no-agent-forwarding,no-X11-forwarding,no-pty,no-user-rc,permitopen="127.0.0.1:37262",command="/bin/false" ssh-ed25519 <隧道公钥>
```

启用：

```bash
systemctl --user enable --now agri-mapping-tunnel.service
systemctl --user status agri-mapping-tunnel.service
journalctl --user -u agri-mapping-tunnel.service -n 30 --no-pager
```

隧道固定开启 `StrictHostKeyChecking=yes`，使用已核验的主机别名，图传 IP 变化不会降低主机密钥验证。它读取地面站 `~/.config/Agri GCS/无人机地面站.conf` 的 `VideoStream/host`，跟随设置或自动发现更新机载地址。桥与隧道均为独立进程，不合并进云台或媒体代理服务。

## 容量与传输

- 桥默认最多每秒处理 5 帧点云，每帧均匀采样最多 8,000 个输入点；可用 `--max-hz 0.5..10` 调整帧率。
- 使用 0.15 米体素去重，机载最多保存 250,000 个点；超限按首次加入顺序淘汰。
- 新客户端先收到当前地图快照，再按顺序接收增量点云。桥快照与增量使用同一锁排序。
- 最多 4 个客户端，各自队列限制 8 MiB/32 帧，发送超时或队列溢出即断开慢客户端；地面站重连后补发快照。
- 每秒发送状态心跳；点云超过 3 秒未更新则过期。位姿最多约 20 Hz。
- 点云二进制数据为每点 16 字节，因此 8,000 点 × 5 Hz 的点云净负载上限约 640 KB/s，另加协议、SSH 和 TCP 开销。首次满量快照约 4 MB。

地面站显示和缓存也有容量限制。导出 PCD 保存当前地面站缓存，不代表无限历史地图或完整测绘成果。清空仅影响地面站缓存；重连后可从机载桥重新获得仍在缓存中的地图。

地面站缓存上限为 250,000 点、10,000 个轨迹位置，渲染最多抽取 80,000 点和 5,000 段轨迹，快照最多每秒更新 10 次。原生 Qt 绘制路径支持虚拟机的 OpenGL 和软件后端；当前是透视点云显示，没有三维实体表面的深度遮挡或网格重建。

## 协议

每帧由 12 字节前缀、UTF-8 JSON 头和可选二进制负载组成：

```text
AGM1 | uint32_le(header_bytes) | uint32_le(payload_bytes) | JSON | payload
```

公共头：`protocol:1`、`type`、`session`（UUID）、`frame`、`stamp`（秒）、`seq`（session 内递增）、`demo`（布尔）。类型包括 `hello/status/reset/cloud/pose`。

- `cloud`：`point_count`、`snapshot`，负载按小端 float32 顺序存储 `x,y,z,intensity`。缓存快照额外携带 `stale`，避免刚连接时把已过期的历史点云误判为实时。
- `pose`：`position:[x,y,z]`、`orientation:[x,y,z,w]`；四元数归一化。
- `status`：`state:waiting_ros/waiting_cloud/live/stale`、`message`、`point_count`。
- `reset`：`reason`；新的 session 用于识别地图坐标来源变化。

`stamp` 保留 ROS 源时间；是否实时由收到的有效点云与单调时钟判断，不依赖机载与虚拟机墙钟严格一致。

## 演示与验证

地面站本地合成演示：

```bash
./build/agri_gcs_visual --mapping-demo
```

桥也支持独立合成源，用于验证真实 TCP/SSH/接收链路；端口需与正式服务分开：

```bash
python3 deploy/pointcloud_bridge.py --demo --bind 127.0.0.1 --port 37263
./build/agri_gcs_visual --mapping-host 127.0.0.1 --mapping-port 37263
```

上述两种演示都必须显示合成数据标记，不能作为真实雷达建图已验证的依据。命令行 `--mapping-host/--mapping-port` 会写入本机地图连接设置，验证后将端口恢复为 `37262`。

无需 ROS 的 Python 单元测试：

```bash
python3 -m unittest discover -s tests -p test_pointcloud_bridge.py -v
python3 -m unittest discover -s tests -p test_mapping_tunnel.py -v
```

覆盖二进制分片、协议限制、体素替换与淘汰、快照和增量顺序、并发快照、时间/坐标重置、慢客户端断开、点云过期、位姿校验和组织点云采样。Qt 测试由 CMake 的 `AGRI_GCS_BUILD_TESTS=ON` 提供。

只读链路检查可在虚拟机执行：

```bash
python3 tools/check_mapping_stream.py --seconds 5
```

`--require-cloud` 可在有点云源时要求收到非空点云；服务正常但 ROS 未运行时，等待状态和零点快照是预期结果。

2026-10-15 验证范围：虚拟机实际桌面三维合成演示和相机图传同时显示；Jetson 正式只读服务经 SSH 隧道返回等待状态；Jetson 独立 ROS master（11321）上的合成标准 ROS 点云/位姿通过桥接器（37263），验证了 900 点快照、增量更新和停止源后的过期快照。该 ROS 测试未启动飞行栈，也不是 MID-360 实景验收。测试工具 `publish_mapping_fixture.py` 强制使用非生产端口的本机 ROS master。

真实机载验收需在操作者自行启动建图后确认：点云与位姿均来自上述话题；地面站显示正确环境和轨迹；断网与重连能够恢复快照；图传与云台仍正常。机载 master 未运行时仅能验证等待状态与连接通路。
