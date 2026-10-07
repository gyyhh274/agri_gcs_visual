# 浙江师范大学金华校区离线地图

位置按浙江师范大学校本部（金华市迎宾大道 688 号）制作，不是杭州校区或行知学院兰溪校区。

- 范围：经度 119.620–119.654、纬度 29.124–29.151，覆盖校区及周边道路。
- 默认中心：经度 119.63764、纬度 29.13678。
- 坐标：WGS84，Web Mercator / EPSG:3857；XYZ，256×256 PNG。
- 缩放：14–19 级，合计 3,138 张瓦片。
- 数据：OpenStreetMap 中的真实道路、建筑、湖泊/绿地等要素，本地程序绘制；不是卫星航拍、不是学校官方测绘图，也不是 AI 编造的校园布局。
- 数据快照时间：接口返回 `2026-06-01T08:52:28Z`，不表示地图已更新到制作当天。开放数据可能不完整或存在误差。

原始数据取自 https://overpass.private.coffee/api/interpreter ，采用一次限定范围查询（不是抓取公共地图瓦片）：

```text
[out:json][timeout:25];way(29.124,119.620,29.151,119.654);out geom;
```

已验证校园对象 `way/297356404` 的 `amenity=university`、名称“浙江师范大学”及迎宾大道 688 号地址。文件 `source-osm.json` 是完整原始导出，`metadata.json` 记录来源、数据时间、范围、级别、数量和源文件 SHA-256。

## 署名和许可

地图数据 **© OpenStreetMap contributors**，按 **Open Database License (ODbL) 1.0** 提供。包内保留了原始数据、署名、许可链接及绘制脚本；使用或再分发地图时请保留来源和许可说明。

- 数据来源与版权：https://www.openstreetmap.org/copyright
- 数据许可：https://opendatacommons.org/licenses/odbl/1-0/
- 公共瓦片使用政策：https://operations.osmfoundation.org/policies/tiles/
- 校本部地址：https://www.zjnu.edu.cn/

本地图在本地由数据生成，没有批量抓取 `tile.openstreetmap.org` 的瓦片；应用运行时也不会访问在线地图服务。

## 重新生成（可选，运行软件不需要 Python）

在项目根目录，安装 Pillow 并指定本机中文字体：

```bash
python3 -m pip install Pillow
python3 scripts/make_campus_tiles.py maps/zjnu-jinhua/source-osm.json /新目录/zjnu-jinhua \
  --font=/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc
```

脚本提供 macOS/Ubuntu 中文字体自动探测。重绘同一份数据不会提升地图实际数据新鲜度；更改绘制样式不等于增加原数据中不存在的建筑或道路。

## 使用边界

此包用于界面开发和本地路线编辑。地图不包含经过验证的障碍物高度、禁飞区、实时限制等飞行安全信息；地图上的点位不能直接作为飞行许可或精密定位依据。飞控通信尚未接入，新增/拖动/保存航点不会自动上传或触发飞行。
