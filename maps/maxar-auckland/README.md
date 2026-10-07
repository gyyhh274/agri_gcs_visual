# 奥克兰周边离线卫星影像

真实 WorldView-2 彩色卫星影像，不是 AI 生成图片，也不是在线地图截图。范围为新西兰奥克兰附近约 5.3 × 5.3 公里，拍摄于 **2022-12-11 22:13:03 UTC**。

本目录包含 EPSG:3857 / WGS84、256×256 像素、13–18 级的 XYZ 瓦片（2,689 张）。完整覆盖的瓦片为 JPEG（质量 90，无色度抽样），透明边缘为 RGBA PNG，以减少存储体积。运行时直接读取磁盘，**不需要联网、账号、Token、Python 或地图服务器**。图外显示网格；边缘无影像部分透明，不会凭空补图。原始影像是单个区域，不是全球卫星地图。

## 来源、许可与限制

- 来源：Maxar Open Data Program，New Zealand Flooding 2023。
- 数据登记：https://registry.opendata.aws/maxar-open-data/
- 原始场景元数据：本目录 `source-stac.json`，原址 https://maxar-opendata.s3.us-west-2.amazonaws.com/events/New-Zealand-Flooding23/ard/60/213311212312/2022-12-11/10300100DE4D9300.json
- 原始 RGB GeoTIFF：https://maxar-opendata.s3.us-west-2.amazonaws.com/events/New-Zealand-Flooding23/ard/60/213311212312/2022-12-11/10300100DE4D9300-visual.tif
- 署名：© Maxar，Maxar Open Data Program。
- 许可：**Creative Commons Attribution-NonCommercial 4.0 International（CC BY-NC 4.0）**。https://creativecommons.org/licenses/by-nc/4.0/ ，完整法律文本见同目录 `LICENSE.txt`。
- 转换说明：本项目将 EPSG:32760 的 RGB GeoTIFF 重投影到 EPSG:3857，采用双线性重采样生成 XYZ JPEG/PNG；转换日期 2026-09-27。JPEG 为有损压缩；透明边缘使用无损 PNG。未使用 AI 超分辨率或伪造地物。

可在遵守署名、保留许可和标明修改等条件下复制、转换并用于**非商业用途**。不能直接用于商业产品或营利演示；需要另行取得授权。本许可仅说明这些卫星影像，不改变其他代码和校园地图各自的许可。

场景元数据的 `gsd` 为 **0.56 m**；RGB 文件输出像素间距为约 **0.305 m**。重采样、缩放以及像素数量增加，不会把原始影像真实细节提升到 0.305 m。

这是历史影像，**不是浙江师范大学，不是实际无人机作业地点**。不提供地形/建筑高度、实时障碍物或禁飞区信息。仅用于离线界面、航点编辑与坐标交互的非商业演示，不可直接用作实际飞行安全依据。

## 启动与地图切换

在项目目录运行：

```bash
# Mac（先构建一次：./scripts/build.sh qmake）
./scripts/run_satellite.sh --builder=qmake

# Orange Pi（先构建一次：./scripts/build.sh cmake）
./scripts/run_satellite.sh --fullscreen
```

地图的 `+ / −`、滚轮、双指缩放、拖动平移以及“添加航点”使用既有离线地图功能。新安装且未保存地图路径时优先加载本目录；卫星启动脚本会覆盖旧的地图路径，但不会改写用户保存的配置或航线。地图切换不会把校园航线转换成新西兰航线；旧航线保留原经纬度，请核对后自行新建、清空或另存。

原校园地图保留在 `maps/zjnu-jinhua`。可通过“地图目录”或设置页面填写其绝对路径切换，也可以用 `./scripts/run.sh --builder=qmake --tiles=/绝对路径/maps/zjnu-jinhua --tile-scheme=xyz` 临时切换。

## 重新制作（仅准备数据时需要）

下面的命令会下载约 61 MB 的源影像，输出目录必须不存在或为空，不会覆盖原地图：

```bash
python3 -m venv /tmp/agri-satellite-tools
/tmp/agri-satellite-tools/bin/pip install rasterio Pillow
/tmp/agri-satellite-tools/bin/python scripts/make_satellite_tiles.py \
  /tmp/maxar-auckland-rgb.tif /tmp/maxar-auckland-xyz --download
```

已有源 GeoTIFF 则去掉 `--download`。分段下载验证每段长度，转换前验证源文件 SHA-256、影像波段、投影与尺寸，生成 `metadata.json` 保存覆盖范围、中心、级别、瓦片数量、来源及许可。源 SHA-256 为 `1094eef0572e6324cdd3653d888c76c5d1e12ae8fc0577a612fd80588f14b248`；本次下载的 S3 multipart ETag 校验也与服务器一致。原始 GeoTIFF 不重复打包，发布包只需要瓦片、元数据及许可说明。发布所附 `source-stac.json` 和 `LICENSE.txt` 应与再生成的瓦片一并保留。
