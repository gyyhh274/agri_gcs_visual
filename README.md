# 无人机地面站 · v1.4.0 离线卫星地图与机载只读 Wi-Fi 遥测

当前先运行在电脑上、后续仍可移植到 Orange Pi 5 Max，使用 **C++17 + Qt 5.15 + QML / Qt Quick Controls 2**。按照用户提供的 7 张参考渲染图重建页面结构、深色配色、三栏布局、表格及控制面板。原 `agri_gcs_qgc_offline_v0857` 工程没有修改。

## 已完成

- 任务执行页：设备状态、机巢状态、任务信息、地图、视频、云台和机巢控制面板。
- 四个任务子视图：航点列表、实时日志、图片列表、视频列表。
- 任务页地图统一高度 450（1536×1024 设计画布的逻辑单位），切换上述四个列表不再改变地图框、可见范围、中心或缩放；窗口整体缩放仍按原比例适配，地图仍可手动缩放和平移。
- 图片列表在下方区域内部滚动，分页栏固定在底部，不会因图片数量增加而挤动地图。
- 日志页：24 条示例事件、关键词/设备/事件类型筛选、分页、选中行与详情联动、图片/视频封面切换。
- 系统设置：连接、无人机、机巢、任务参数、地图、告警与安全、存储与数据、系统维护；输入框和开关可以编辑，配置可保存到本机用户配置。保存“无人机设置”后，任务页设备名称、型号、飞控类型及连接概览中的型号/飞控会读取同一份已保存配置，重启后仍有效；未保存的输入不会同步到任务页。
- 两种设置布局：带左侧导航的设置页、无左侧导航的总览页。双击顶栏“系统设置”可切换两种布局。
- 航点编辑/删除、JSON 导入校验、本地航线保存、图片选择与放大预览、云台角度/变焦演示。
- 异步离线 XYZ/TMS 地图：滚轮/按钮/双指缩放、拖动平移、鼠标位置锚定缩放；磁盘读取和图片解码在受限线程池中执行，支持 Qt Quick 软件渲染。
- 地图点击新增航点、拖动航点修改经纬度、编号/连线/表格同步、选中联动、确认清空和保存后重启恢复；最多 200 个航点。
- 附带浙江师范大学**金华校区**及周边离线地图：14–19 级、3,138 张 PNG，由真实 OpenStreetMap 道路/建筑数据本地绘制，不是卫星影像。
- 新增真实离线卫星影像：新西兰奥克兰周边约 5.3×5.3 km，WorldView-2 / Maxar，拍摄时间 2022-12-11，13–18 级、2,689 张 XYZ JPEG/PNG。来源地面分辨率约 0.56 m；支持完全离线加载和航点编辑。影像限非商业用途，已保留来源、许可和转换说明。
- 地图显示独立的青绿色机巢图标和名称，随缩放、平移准确定位；支持点击查看 WGS84 坐标、定位机巢、地图选点并确认保存，以及机巢设置页填写坐标。手动位置重启恢复，不随地图源切换，不会作为航点或被清空航线删除。
- 1536×1024 基准画布，按窗口和触摸屏尺寸等比例缩放；支持全屏和软件渲染。

这是**可离线规划地图与本地交互版本**。地图已替换为真实坐标的离线瓦片，地图与航点编辑是真实本地功能；视频仍是示例封面，图片列表复用参考图内的照片区域。新增的机载 Wi-Fi 链路只读取 ROS/PX4 连接、模式、电池、任务就绪与 `world` 位置；只有收到有效机载遥测才显示无人机“已连接”。任务控制、热成像、机巢和相机仍未接入，不会向飞控、机巢或相机发出指令。真实 MAVLink、RTSP、云台协议、机巢协议和文件下载/导出尚未实现。未配置时显示的示例型号/IP/固件版本来自参考图，不代表设备的实际版本；即使设备资料同步，也不表示已从硬件自动识别型号。地图数据不是实测成果，不包含可靠的障碍物高度、禁飞区或实时安全信息，不能直接据此飞行。

## 电脑与机载电脑 Wi-Fi 接入（首阶段，只读）

机载 Diff-Planner 工程新增 `src/user_command/multipoint/scripts/ground_station_gateway.py`。它订阅机载 ROS1 的 `/mavros/state`、`/mavros/battery`、`/ekf/ekf_odom`、`/px4ctrl/mission_ready`，每 0.5 秒通过 TCP 输出一行 JSON 遥测。**它不接收任何飞行或任务指令，也不依赖旧 Orange Pi 触屏软件。** 实体遥控器仍保留安全接管。

推荐通过 Wi-Fi 上的 SSH 隧道连接，不向局域网暴露服务。先在机载电脑已有 ROS1 环境和现有飞行栈启动后运行（路径换成机载实际路径）：

```bash
source /opt/ros/noetic/setup.bash
source /实际路径/diff-planner-A8mini-master/devel/setup.bash
python3 /实际路径/diff-planner-A8mini-master/src/user_command/multipoint/scripts/ground_station_gateway.py --bind 127.0.0.1 --port 8765
```

随后在电脑终端运行（将用户名和 IP 换成机载电脑实际值）：

```bash
ssh -N -L 8765:127.0.0.1:8765 用户名@机载电脑IP
```

在新地面站“系统设置 → 连接设置”中填入 `127.0.0.1`、`8765`，点击“连接设备”。若已有保存配置覆盖了新默认值，请手动改为上述地址和端口。界面只有在收到协议正确且未超时的遥测后才显示连接；网络中断时状态会复位。`world` 坐标是机载局部定位，**不是经纬度**。目前地图航点为 WGS84 经纬度，尚无到 `world` 的标定，故“上传航线”“开始任务”“暂停”“返航”均不发送命令。不要把当前离线卫星地图上的测试航点当作真实飞行任务。

机载电脑需要 Python 3 的 `rospy`、`mavros_msgs`、`nav_msgs`、`sensor_msgs`、`std_msgs`，以及已运行的 ROS master；此部署命令尚未在用户机载硬件上验证。若使用非 Noetic 的 ROS1 发行版，先确认其 Python 3 ROS 包可用。不要在未受控的 Wi-Fi 上使用 `--bind 0.0.0.0`：该首版 TCP 遥测流本身没有加密或认证。下一阶段将设计有鉴权与确认机制的任务接口，并先解决地图经纬度与机载 `world` 坐标的标定。

## 离线地图启动与航线规划

项目内 `maps/maxar-auckland/` 已包含卫星地图，`maps/zjnu-jinhua/` 保留原校园地图，不需要运行时下载。复制项目时必须连同整个 `maps/` 目录复制。香橙派首次更新需要安装 Qt Positioning 依赖并重新构建。

**卫星地图启动（推荐）**：

```bash
# Mac
./scripts/build.sh qmake
./scripts/run_satellite.sh --builder=qmake

# Orange Pi / Ubuntu 22.04
./scripts/install_deps_ubuntu22.sh
./scripts/build.sh cmake
./scripts/run_satellite.sh --fullscreen
```

卫星启动脚本使用绝对路径，可从任意目录调用，并覆盖之前保存的地图目录，仅对本次运行生效。若程序已经打开，需要关闭旧窗口再启动新版。旧校园航线不会自动转换为新西兰航线，不会清除或覆盖用户保存的航线；地图上规划的航点只是演示数据，请核对实际作业位置。

原校园地图启动：

```bash
./scripts/install_deps_ubuntu22.sh
./scripts/build.sh cmake
./scripts/run.sh --fullscreen --tiles=maps/zjnu-jinhua --tile-scheme=xyz
```

本机 Mac：

```bash
./scripts/build.sh qmake
./scripts/run.sh --builder=qmake --tiles=maps/zjnu-jinhua --tile-scheme=xyz
```

有图形驱动问题加 `--software`。无额外参数时优先采用之前配置的目录，再自动寻找随包卫星地图（无卫星目录则尝试校园地图）；显式传入 `--tiles` 可以覆盖旧目录。自有地图也可以通过 `AGRI_GCS_TILE_DIR=/绝对路径`、地图上的“地图目录”，或“系统设置 → 地图设置 → 保存设置”接入。界面配置会保存，命令行路径只对本次启动生效。

操作：

1. 地图顶部 `+ / −`、滚轮或双指缩放；拖动空白位置平移。
2. 点击“添加航点”，再点击地图空白处创建航点；点击“结束添加”退出添加模式。
3. 点击标记选中；拖动标记更新经纬度，列表和连线同步更新。表格中的编辑/删除按钮同样联动地图。
4. “航线范围”显示整条航线；“地图范围”显示瓦片覆盖范围。
5. 点击“保存航线”后重启或返回任务页可以恢复。未保存的修改离开任务页会丢失。默认不创建任何可飞行航线；以前保存的演示航线会保留，请确认坐标后清空或另行规划。

新增航点初始高度 120 m、速度 8 m/s 等是编辑默认值，必须自行修改核对；没有上传或执行到真实飞控。清空操作有确认窗口，且不会清除机载任务；清空后需要再次保存才会覆盖本地存储。

### 机巢位置显示与配置

未设置位置时显示 **“示例机巢（非真实位置）”**，位于当前地图中心。这只是界面示例，不声称该地点有真实机巢，切换地图时仅此示例位置跟随新的地图中心。旧版机巢设置中的参考坐标不会自动作为真实机巢位置使用。

- 点击地图上的青绿色房屋图标可显示名称、纬度、经度以及“非实时定位”说明，再点图标或信息卡可关闭。
- “定位机巢”将地图移到该位置；若保存的位置在离线影像之外，会显示缺失瓦片网格，不会伪造影像或把真实坐标移动到地图内。
- “设置机巢”进入选点模式，在地图点击目标位置，在弹窗里核对名称与 **WGS84** 经纬度后点击“保存位置”；取消不会修改位置。选点不会新增航点，也不会修改航线。
- 或在“系统设置 → 机巢设置”填写名称、经度、纬度，点击“保存设置”；地图与各页面标记共享同一份位置，下一次启动会恢复。纬度须在 ±85.05112878 内、经度在 ±180 内，空白、非数字、无穷或越界值拒绝保存，旧位置不变。

手动位置保存在独立的 `NestPosition` 本机配置项中。此功能没有接入机巢 GPS/在线状态、没有向设备下发指令；名称旁的“手动”不是“已连接”或“实测”的含义。当前地图为新西兰卫星影像，请不要用其示例位置替代实际设备坐标。

支持数字 `z/x/y.png`（或小写 jpg/jpeg/webp），XYZ 与 TMS 的区别是 Y 方向。坐标为 **WGS84，经投影 EPSG:3857**；不能直接混用 GCJ-02/BD-09 瓦片。当前不直接读取 MBTiles，也没有在线搜索或在线地图回退。缺失的缩放级别/覆盖区域显示网格，损坏图片只提示读取失败，不阻塞其他按钮。后台扫描目录，最多 2 个解码线程、10 个请求、96 张内存缓存（可通过旧控件的 AGRI_GCS_TILE_* 环境变量调整）。

卫星地图的制作、数据时间、来源、**CC BY-NC 4.0 非商业限制**及分辨率说明见 [卫星地图说明](maps/maxar-auckland/README.md)。原校园地图见 [校园地图说明](maps/zjnu-jinhua/README.md)。这些历史影像不能作为实际无人机飞行安全依据。

## Orange Pi / Ubuntu 22.04

将整个项目目录复制到香橙派，在项目目录运行：

```bash
chmod +x scripts/*.sh
./scripts/install_deps_ubuntu22.sh
./scripts/build.sh cmake
./scripts/run.sh --fullscreen
```

安装依赖需要 sudo。构建、运行无需 sudo。Qt Creator 也可以直接打开 `CMakeLists.txt` 或 `agri_gcs_visual.pro`。

硬件图形驱动不可用时：

```bash
./scripts/run.sh --fullscreen --software
```

SSH 启动到本机桌面时，需要设置实际桌面的 `DISPLAY`，例如 `DISPLAY=:0 ./scripts/run.sh --fullscreen`。本项目不依赖网络地图服务；Qt 软件渲染可以显示全部界面，但帧率仍取决于设备。

## qmake / 本机预览

```bash
./scripts/build.sh qmake
./scripts/run.sh --builder=qmake
```

构建目录分别为 `build-cmake/` 和 `build-qmake/`，避免两个构建系统混用缓存。macOS 脚本能识别 `.app/Contents/MacOS/` 中的可执行文件。香橙派需在其 Ubuntu/ARM64 环境重新编译；本机编译结果不能直接作为香橙派二进制使用。

可选参数：

| 参数 | 功能 |
|---|---|
| `--page=0/1/2` | 任务 / 日志 / 设置 |
| `--tab=0..3` | 航点 / 实时日志 / 图片 / 视频 |
| `--section=0..7` | 设置侧栏；日志侧栏为 0..5 |
| `--overview --page=2` | 无左侧导航的设置总览 |
| `--size=1280x800` | 设置窗口逻辑像素尺寸 |
| `--fullscreen` | 全屏 |
| `--software` | Qt Quick 软件渲染 |
| `--tiles=/绝对路径` | 本次启动采用的离线瓦片目录 |
| `--tile-scheme=xyz/tms` | 本次启动采用的瓦片 Y 方向 |
| `--screenshot=/绝对路径/page.png` | 保存实际渲染截图后退出 |

无桌面验收示例：

```bash
QT_QPA_PLATFORM=offscreen ./scripts/run.sh --builder=qmake --software \
  --size=1536x1024 --page=0 --tab=2 --screenshot=/tmp/photos.png
```

## 页面预览

`previews/` 中的图片是软件实际渲染结果：

- `task-waypoints.png`、`task-live-log.png`、`task-photos.png`、`task-videos.png`
- `logs.png`、`settings.png`、`settings-overview.png`

上述七张图片保留为 v1.0 界面复刻的历史比对图。v1.1 实际渲染见 `offline-campus-route.png` 和 `offline-map-settings.png`；v1.2 见 `offline-satellite-route.png`（卫星影像加载并通过鼠标新增/拖动后的测试航线）。字体、图标和图像清晰度会受系统字体及参考素材分辨率影响，当前版本没有声称逐像素一致。

v1.3 机巢标记实际渲染见 `previews/offline-satellite-nest.png`，位置为测试时手动选点的演示坐标，不写入用户配置。

## 验证

本机 **macOS 15.7 / ARM64 / Qt 5.15.2 / qmake** 编译通过。v1.2 在 macOS `sandbox-exec` 禁止网络访问的进程环境中验收：Qt UI 测试覆盖 18 种页面/侧栏、航点导入/编辑/删除/重启恢复、真实卫星 JPEG/PNG 加载、18 级实际渲染、13–18 级缩放边界、像素与坐标往返、鼠标锚定缩放、真实窗口鼠标点选/拖动/滚轮、合成双指缩放、XYZ/TMS、目录切换与覆盖范围，以及日志筛选和配置保存，共 **28 项通过、0 项失败、0 项跳过**（含测试初始化和清理）。测试结果见 `tests/satellite-verification.txt`。原校园地图单独回归为 27 项通过、0 项失败，卫星专项按环境跳过 1 项。

v1.3 新增机巢坐标校验/保存恢复、标记缩放平移对齐、点标记不误添加航点、地图选点确认与取消、设置页联动与异步示例坐标同步、清空航线不删除机巢、无图时隐藏标记等验收。在禁止网络访问环境中 **31 项通过、0 项失败、0 项跳过**；结果见 `tests/nest-verification.txt`。仍未在 Orange Pi 真机或本机 CMake 构建环境验证。

v1.3.1 在 1536×1024、1280×800、1920×1080 三种窗口尺寸下，通过真实鼠标连续切换四个列表，检查同一个地图实例、地图与列表边框、菜单位置、中心、缩放、四角经纬度、机巢屏幕位置及航线数据均不变；图片列表内部滚动不影响地图。新增无人机名称/型号/飞控从已保存设置同步、未保存草稿不生效，以及从本机配置恢复的测试。全套禁网测试 **35 项通过、0 项失败、0 项跳过**，见 `tests/verification-v1.3.1.txt`。`previews/fixed-map-tab-0..3.png` 为四种列表的实际截图，其地图区域像素一致；`previews/aircraft-profile-synced.png` 使用本机先前保存的 `ZJNU_AIR` 型号实际渲染。窗口缩放会整体按比例改变页面，不代表地图被锁死。

v1.4.0 增加机载只读 ROS/TCP 遥测接入、真实连接状态和旧演示端口迁移。本机 Qt/qmake 编译通过；界面回归 **36 项通过**，独立 TCP 协议测试 **4 项通过**，机载网关 Python 单元测试 **4 项通过**。尚未在实际机载电脑、PX4 或 Wi-Fi 链路上验证；飞行任务指令仍未实现。

卫星影像下载的 multipart ETag 与服务器一致，SHA-256 已记录并在再生成脚本中校验；2,689 张瓦片全部验证为 256×256、可完整解码且坐标索引合法，无重复索引。包含 2,346 张 JPEG 和 343 张透明边缘 PNG，瓦片文件共 74,069,065 字节。测试截图内两个航点只是交互验收数据，不作为用户默认航线，既有用户航线和地图设置未清除。尚未在 Orange Pi 5 Max 真机执行，也未在本机执行 CMake 构建。

可选测试：

```bash
mkdir -p build-tests
cd build-tests
qmake ../tests/ui_smoke.pro
make -j4
AGRI_GCS_TEST_TILES="$(pwd)/../maps/maxar-auckland" \
AGRI_GCS_TEST_SATELLITE="$(pwd)/../maps/maxar-auckland" \
QT_QPA_PLATFORM=offscreen ./ui_smoke
```

CMake 也提供 `-DAGRI_GCS_BUILD_TESTS=ON` 测试选项。

## 结构与后端接入

```text
src/main.cpp                Qt 入口、参数、字体和实际截图
src/AsyncTileMapItem.*       复用旧项目的异步本地栅格地图控件
src/OfflineMapSource.*      离线目录扫描、格式、范围和本机配置
src/NestPosition.*          手动机巢坐标、示例状态、校验与持久化（非遥测）
src/AircraftProfile.*       从已保存界面配置加载无人机名称、型号及飞控，保存后即时同步
src/GroundLink.*            电脑到机载 ROS 网关的只读 TCP 遥测客户端与超时检测
qml/Main.qml                主导航、窗口和尺寸缩放
qml/pages/TaskPage.qml       任务页与四个子视图
qml/pages/LogPage.qml        日志检索与详情
qml/pages/SettingsPage.qml   设置与本地配置
qml/components/             卡片、按钮、字段、地图交互和视频示例等
assets/                     用户参考图整理的演示素材
scripts/                    Ubuntu 依赖、构建和运行
scripts/make_campus_tiles.py 根据开放地理数据生成校园地图瓦片
scripts/make_satellite_tiles.py 下载并重投影真实卫星 GeoTIFF 生成 XYZ 瓦片
scripts/run_satellite.sh     使用随包卫星地图启动，覆盖旧地图路径
maps/maxar-auckland/         离线卫星瓦片、源影像元数据与非商业许可
maps/zjnu-jinhua/            校园离线瓦片、原始数据快照与许可说明
tests/                      Qt UI 冒烟测试
previews/                   实际渲染预览
```

真实功能可通过 C++ `QObject` 后端的 `Q_PROPERTY` / `Q_INVOKABLE` 接入 QML。后续可实现飞控通信、视频流、机巢和云台服务，将界面演示状态替换成设备数据。离线地图已经接入。设置目前保存到 Qt 的用户配置；敏感凭据如 RTK 密码不会保存，设置参数没有修改系统时钟、亮度、自启动或硬件配置。
