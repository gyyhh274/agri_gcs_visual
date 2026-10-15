# 室内局部地图与远程航点首版

使用 `world` 米制网格在地面站选点，通过 TCP JSON-lines protocol 2 上传到机载 ROS1 任务网关，再由 `multipointplan` 和 Diff-Planner 执行。

当前为源码首版：协议及坐标逻辑已有检查，尚未完成 Qt5/ROS1 完整编译、规划器仿真和实机验收。此仓库包含地面站端；运行任务需要配套的机载 remote 模式与 protocol 2 网关。原 protocol 1 网关仍只能提供只读遥测。

## 地图与定位

无需 GPS，但需要雷达或视觉里程计提供有效的 `world` 位姿。网格只是选点画布，不提供定位、障碍物信息或规划轨迹。

- 默认每格 1 m，缩小时每格 2 m。X 向右、Y 向上，对应本次定位坐标轴，不代表东/北或机头方向。
- 点击“添加航点”后在网格选点，拖动航点修改 X/Y，编辑 Z 和悬停时间。
- 默认 Z=1 m、悬停 2 s。Z 是 `world.z`；只有地面对应 Z=0 时，Z=1 才表示离地约 1 m。
- 青绿色点显示有效机载里程计中的无人机位置；“定位无人机”将视口移到该位置。
- 橙色连线表示航点顺序，实际避障地图和轨迹继续在 RViz 查看。
- 室内航线单独保存在 `IndoorMissionV1`，与原有经纬度航线分开。室外航线仍只支持本地编辑。

定位重启或原点改变后，重新核对 RViz 中的原点、轴向和高度，再确认并上传航线。快速重置和小幅坐标变化不保证被自动识别，应按机载流程重启完整定位、任务和控制链路。

## 演示步骤

1. 启动配套机载 remote 任务模式与网关；通过 SSH 隧道将机载 loopback 的 8765 端口映射到地面站。
2. 在连接设置填写 `127.0.0.1:8765`，确认收到有效定位和任务状态。
3. 在网格选点，编辑局部高度，勾选已核对本次定位坐标，点击“上传航线”。上传不触发起飞。
4. 使用原遥控器流程起飞悬停，等待机载任务就绪，再点击“开始任务”。
5. 查看当前航点、悬停阶段和已完成数。起飞、降落及人工接管沿用原流程。

开始仅在机载任务、版本、定位会话和当前编辑航线均匹配时启用。编辑航线后需重新上传；断线重连后先查询机载状态，不自动重发开始命令。

首版支持上传、开始和查询。暂停、取消、执行中追加/替换、自动返航、界面降落和航点云台动作未接入，相关飞行按钮禁用。速度由机载规划器配置，未提供逐点速度设置。

## 网关约定

地面站保留 protocol 1 只读遥测支持。protocol 2 增加任务状态及命令确认，所有消息为 UTF-8 JSON，以换行分帧，单帧上限 64 KiB，单任务最多 200 航点。

命令操作为 `upload`、`start`、`get_status`，包含 request_id、map_session_id、mission_id 和 revision。上传使用 `frame_id=world`，航点字段为 id、x、y、z、hover_sec。

任务状态包含定位会话、任务 ID/版本、state、current_waypoint_id、completed、total、reason、odom_fresh、flight_ready、world_x/y/z、max_abs_xy、min_z、max_z。state 支持 IDLE、READY、FLYING、HOVERING、COMPLETED、FAILED、LANDING。完整字段及校验逻辑见 GroundLink 和测试。

命令确认超时为 5 s，遥测超时为 3.5 s。超时提示“结果未知”，不等同于机载拒绝或任务停止。ACK 仅表示任务层接受，不保证规划成功。仅 Wi-Fi 断线时，首版机载端在定位和控制正常的情况下继续已开始的任务；自动断线悬停需要后续可恢复暂停机制。

## 验证

已执行实际 QML JavaScript 函数检查：航线编辑、错误导入、会话与开始门禁、像素与 world 坐标换算、锚点缩放通过。此检查不等于 Qt/QML 界面加载或渲染验证。

```bash
node tests/test_indoor_logic.js
```

在已有 Qt5.15 环境中构建原地面站，并执行通信测试：

```bash
./scripts/build.sh qmake
mkdir -p build-ground-link-test
cd build-ground-link-test
qmake ../tests/ground_link.pro
make
./ground_link_test
```

通信测试覆盖 protocol 1 遥测兼容、protocol 2 确认关联、定位会话变化、超限高度和断线清理。本次未运行 Qt5 测试；需在目标环境继续验证完整编译、QML 页面、机载 ROS1 服务、规划仿真和实机闭环。
