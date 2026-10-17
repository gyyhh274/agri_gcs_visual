# 打开地面站自动建图

默认开启：打开地面站 → 通过机载 SSH 隧道发送建图请求 → 显示启动进度 → 接收真实点云，出现 LIVE。

使用前给机载电脑、飞控和 MID-360 供电并连接对应 USB、雷达网线和实验室网络。机载 IP 变化时在图传连接设置中更新地址，地图隧道会跟随 `VideoStream/host`。不需要另外打开 SSH 窗口手动执行建图脚本。

- 实时建图顶部“启动建图”按钮用于手动请求或失败后重试。
- “实时建图 → 连接 → 打开地面站时自动启动建图”开关会持久保存，默认勾选。
- 机载暂时不可达时重试连接；传感器启动失败时显示原因，修复后手动重试。
- 重复打开地面站只检查并复用健康建图，不重启现有雷达进程。
- 关闭地面站或关闭自动启动开关，不停止已运行建图。
- 演示、截图、非默认地图地址/端口不会发起启动请求。只想连接数据而不请求启动，可用 `--no-mapping-autostart`。

启动范围为 ROS master、MAVROS、MID-360 和 Faster-LIO，不启动规划器、任务执行器、px4ctrl，也不发送解锁、模式切换或起飞命令。

## 安装位置与协议

机载使用 `/home/ly/Documents/diff-planner-A8mini-gcs-mission`（`zjc-argc`），不操作旧目录。

- 启动 API：`agri-mapping-start.service`，机载 `127.0.0.1:37263`，API 开机运行但等待请求才启动传感器。
- 点云桥：仍使用 `agri-pointcloud-bridge.service`，`127.0.0.1:37262`，保持只读。
- 虚拟机：`agri-mapping-tunnel.service` 同时转发上述两个端口，使用已核验的 SSH 主机密钥和受限专用客户端密钥。
- 任务网关的 `8765/protocol 2` 独立，不参与一键建图。

`MappingStarter` 通过 HTTP POST `/ensure` 发送固定 JSON：

```json
{"protocol":1,"operation":"ensure_mapping"}
```

随后 GET `/status`，验证 `service=agri-mapping-start`、`protocol=1` 和 `idle/starting/ready/error`。网络请求异步执行，不阻塞 QML。启动接口的 ready 不替代点云新鲜度判断，地图 LIVE 仍由实际数据决定。

机载实现、部署步骤及构建说明位于机载仓库的 `docs/GCS一键建图.md`，入口为 `sh_files/build_mapping_only.sh`、`sh_files/start_mapping_only.sh` 和 `tools/mapping_start_service.py`。
