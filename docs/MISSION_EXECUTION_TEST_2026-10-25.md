# 航点任务执行逻辑对照测试

日期：2026-10-25

## 范围与结论

参考 `D:\Users\ZhengYue\Documents\workspace\agri_gcs_visual` 的任务事务处理方式，在虚拟机 `/home/cat/projects/agri_gcs_visual` 的 `codex/indoor-point-roundtrip` 分支上验证。测试基线为 `f4d2f4c769ffe79b6c48229f9327e6a290b5bd5d`。

参考重点为上传与开始分离、请求回执关联、任务 ID/版本/定位会话核对，以及超时和断线时不自动重发。参考目录仅被读取，没有修改。

本次保留现有雷达 LIVE 建图、点云俯视选点、world 坐标、`[P,R]` 双航点顺序、`automatic=true`、`ground_origin`、起降高度和独立确认开始的方式。没有移植参考版本的多航点网格编辑、动态更新、追加续飞或独立返航功能。

## 发现与最小修复

修复前新增测试结果为 **35 passed / 6 failed**，包含初始化与清理。6 个失败场景均来自通信层：直接调用 `GroundLink::sendCommand` 时，旧定位会话的上传/开始、不同任务 ID 的开始/取消、不匹配版本的开始/取消，仍可能产生发送请求。原有 `RoundTripMission` 上层已核对任务身份，失败说明通信层缺少独立防护，不等于已在实际界面或真机触发误执行。

在 `src/GroundLink.cpp` 增加 4 行校验：

- 除 `get_status` 查询外，命令会话必须与当前机载遥测一致。
- `start` 和 `cancel` 的任务 ID、正整数版本必须与当前机载任务一致。

补强后的请求格式、端口、协议和有效任务流程保持一致。新增校验只在过期或不匹配的请求产生网络写入前返回。

## 测试结果

**完整构建成功；网络隔离下 9 组 CTest 全部通过，失败 0 组，总耗时 98.44 秒。**

新增业务场景修复后全部通过，QtTest 汇总 **41 passed / 0 failed / 0 skipped**；其中包含初始化与清理 2 项。定向复测的 6 个身份错配场景也全部通过。

新增 `mission_execution_test` 共 **39 个业务场景**；QtTest 汇总包含初始化、清理，共 41 项：

| 范围 | 业务场景数 | 核验内容 |
| --- | ---: | --- |
| 上传、开始、完成 | 1 | 原有 P/R 顺序、地面原点与返航高度；上传回执不会开始；重复开始被拦截；起飞/飞行/悬停/降落状态显示 |
| 上传回执关联 | 8 | 错误请求 ID、任务 ID、版本、会话、点数、状态、自动标记和拒绝回执均不放行开始 |
| 草稿和起飞前条件变化 | 12 | 目标、高度、地图/任务会话、围栏、位置漂移、起飞高度、地图暂停、坐标核对、定位、控制器和解锁状态变化阻止开始 |
| 显式取消和失败锁定 | 5 | READY、TAKING_OFF、FLYING、HOVERING、LANDING 阶段取消；FAILED 后不重新上传或开始 |
| 超时与迟到回执 | 1 | 实际等待 5 秒命令超时；迟到的成功回执不恢复开始资格，也不自动重发 |
| 断线重连 | 1 | 待确认开始时断线，结果显示未知；重连和重建任务对象均不重放开始，只在明确查询时发送 get_status |
| 通信层过期身份拦截 | 6 | 旧会话、不同任务 ID、不匹配版本的命令不进入网络发送 |
| protocol 1 兼容 | 1 | 旧协议只读，不能发送任务控制命令 |
| 畸形回执 | 4 | 非 world、过量航点、异常完成数、非数值起飞高度不消费待确认请求 |

原有点云投影、建图客户端、启动器、桥接、隧道、任务链路、往返流程和界面测试一并回归。

## 隔离与证据边界

测试通过 `sudo unshare --net` 创建只有 loopback 的独立网络空间，再用 `runuser -u cat` 降权运行。模拟任务网关和点云桥仅监听随机回环端口，没有调用生产任务隧道入口，也不能访问宿主网络中的机载设备。没有向真实 8765、ROS master 或飞控发送任务，没有启动真实飞行栈或重启地面站。

测试验证地面站客户端的协议、开始门禁、回执和状态处理。起飞、降落等机载状态由测试网关注入；不覆盖真实机载任务执行器、轨迹规划器、控制器或实际飞行效果。

## 代码差异

- `src/GroundLink.cpp`：4 行通信层身份校验。
- `tests/tst_mission_execution.cpp`：新增隔离模拟测试。
- `CMakeLists.txt`：新增测试目标与 180 秒测试超时。
- `docs/MISSION_EXECUTION_TEST_2026-10-25.md`：本记录。

使用 `git diff --exit-code` 核对，`RoundTripMission.*`、`MappingClient.*`、`MappingStarter.*`、点云显示/几何模块、全部 QML、部署脚本和原有 `tests/tst_roundtrip.cpp` 均与基线一致。新增测试代码不进入正式应用目标。

改动保留在当前分支工作区，本轮未提交或推送 GitHub。

## 复现命令

在虚拟机中执行：

```bash
cd /home/cat/projects/agri_gcs_visual
cmake -S . -B build-tests -DAGRI_GCS_BUILD_TESTS=ON
cmake --build build-tests -j2
sudo unshare --net bash -c 'ip link set lo up; exec runuser -u cat -- ctest --test-dir /home/cat/projects/agri_gcs_visual/build-tests --output-on-failure'
```

修复前记录：`build-tests/mission-execution-before.txt`。

6 个问题场景的修复后定向记录：`build-tests/mission-stale-identity-after.txt`。

完整回归记录：`build-tests/mission-execution-ctest-after.txt`；逐项 QtTest 输出见 `build-tests/Testing/Temporary/LastTest.log`。