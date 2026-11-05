# PLC 地面站验证记录 · 2026-11-01

本次交付完成 Mac 地面站的 Modbus TCP 通信与舱门上层接口；PLC 程序未下载、硬件未重启、真实控制寄存器未写入。真实设备只执行 TCP 连接和 FC43/14 只读设备识别。

## 现场证据

[原始 JSON](plc-live-probe-2026-11-01.json) 来自以下命令：

```bash
python3 tools/plc_probe.py
```

- Mac：`192.168.2.28`，接口 `en0`；目标 `192.168.2.8:502`。
- Unit ID 1 试探请求：`00 01 00 00 00 05 01 2b 0e 01 00`。
- 响应：`00 01 00 00 00 03 01 ab 01`，有效异常 `01`（功能不支持）。
- 结论：TCP 与 Modbus TCP 请求/响应链路均通过；寄存器读取、设备身份和舱门控制尚未在真实 PLC 上验证。
- 下一步：根据 PLC 项目核对 Unit ID 和一个安全只读 PDU 地址，在设置页读取一次。业务开发按 [接口约定](PLC_MODBUS.md) 落地，再联调实际控制。

## 构建与回归

使用项目既有 Qt 5.15.2 / C++17 / CMake 工具链，macOS x86_64 构建成功。主程序及测试目标已重建；CMake 与 qmake 项目文件均已接入新源文件。qmake 构建本次未单独执行。

以下命令需 `cmake` / `ctest` 已在开发环境 PATH 中，Qt 路径沿用现有 CMake 缓存。应用的常规构建入口仍为 `./scripts/build.sh cmake`。

```bash
cmake -S . -B build-cmake -DAGRI_GCS_BUILD_TESTS=ON
cmake --build build-cmake --parallel 4
ctest --test-dir build-cmake --output-on-failure -j 3
```

最终结果：**6 / 6 个测试目标通过，0 失败**。

| 测试目标 | 结果 | 验证内容 |
| --- | --- | --- |
| nest_plc_test | 19 passed / 0 failed | 含初始化与清理；协议异常、分片、边界、错误响应、FC16 回显、超时不重发、配置、门控、故障/急停下停止、心跳失效、丢失回执与 Python 模拟器端到端 |
| ui_smoke | 37 passed / 0 failed / 0 skipped | 含初始化与清理；PLC 按钮、空地址、整数越界、读值、导航保留连接、禁用未就绪动作；现有页面、地图、图片滚动与设置 |
| onboard_manager_test | 通过 | 机载模块管理回归 |
| ground_link_test | 通过 | 任务通信回归 |
| gimbal_link_test | 通过 | 云台通信回归 |
| task_session_test | 通过 | 航线编辑、回执与页面切换回归 |

测试使用临时用户配置与回环端口。UI 测试补齐了现有上下文依赖，使用本机图片数据验证滚动布局，并显式选择室外编辑模式验证历史地图用例；室内会话由独立测试覆盖。

此外以 Python 只读工具连接本机分片响应模拟器，验证识别异常 01、地址 100 四字读取成功、未映射地址 99 返回异常 02。Python 工具语法检查与 `git diff --check` 通过。

## 界面验证

[机巢设置实际渲染](../previews/plc-settings.png) 已检查：目标为 `192.168.2.8:502`，映射地址未分配，监测与动作按钮默认禁用，通信内容可滚动。截图是未连接时的界面预览，不作为真实寄存器通信证据。

界面渲染使用：

```bash
QT_QPA_PLATFORM=offscreen ./build-cmake/agri_gcs_visual.app/Contents/MacOS/agri_gcs_visual --software --start-page 2 --start-section 2 --screenshot previews/plc-settings.png --screenshot-delay 1000
```

真实 PLC IP 的断电持久化、寄存器映射、PLC 控制程序、现场限位/联锁/急停及电机动作仍需后续验证。
