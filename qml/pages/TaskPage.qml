import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt.labs.settings 1.1
import QtMultimedia 5.15
import "../components"

Item {
    id: page
    property int contentTab: 0
    // 启动时默认展示的页签（可用 --start-tab 覆盖，便于截图与自动化验证）
    property int startupTab: initialTab
    property string missionState: indoorMode ? missionLabel() : "室外航线仅本地编辑"
    property bool indoorMode: true
    property int localSelected: -1
    property int localRevision: 0
    property string reviewedSession: ""
    property string uploadedLocalJson: ""
    property string uploadedMissionId: ""
    property int uploadedMissionRevision: 0
    property string uploadedSession: ""
    property string pendingUploadJson: ""
    property string pendingUploadRequest: ""
    property string pendingUploadSession: ""
    property string pendingUpdateJson: ""
    property string pendingUpdateRequest: ""
    property string pendingUpdateMissionId: ""
    property string pendingUpdateSession: ""
    property int pendingUpdateRevision: 0
    readonly property bool missionActive: groundLink.remoteAvailable && ["FLYING", "HOVERING", "LANDING"].indexOf(groundLink.mission.state) >= 0
    readonly property bool landingLocked: groundLink.remoteAvailable && groundLink.mission.state === "LANDING"
    readonly property bool confirmedRoute: groundLink.remoteAvailable && uploadedLocalJson.length > 0 &&
        uploadedSession === groundLink.mission.map_session_id && uploadedMissionId === groundLink.mission.mission_id &&
        uploadedMissionRevision === groundLink.mission.revision
    readonly property bool dynamicEditable: confirmedRoute && reviewedSession === groundLink.mission.map_session_id &&
        ["FLYING", "HOVERING"].indexOf(groundLink.mission.state) >= 0
    readonly property bool continuationMode: confirmedRoute && reviewedSession === groundLink.mission.map_session_id &&
        (groundLink.mission.state === "COMPLETED" || (groundLink.mission.state === "READY" && groundLink.mission.completed > 0))
    readonly property bool updateMode: dynamicEditable || continuationMode
    readonly property bool canReturnLocal: indoorMode && groundLink.remoteAvailable && !groundLink.busy && groundLink.armed &&
        groundLink.odomFresh && ["READY", "FLYING", "HOVERING", "COMPLETED"].indexOf(groundLink.mission.state) >= 0 &&
        groundLink.mission.mission_id.length > 0
    // LANDING locks remote commands, but edits here only change the local draft.
    readonly property bool localEditable: !groundLink.busy && (!missionActive || landingLocked || dynamicEditable)
    readonly property bool localReplaceable: !groundLink.busy && (!missionActive || landingLocked)
    readonly property int lockedPrefixCount: continuationMode ? groundLink.mission.completed : missionActive && !landingLocked ? Math.min(groundLink.mission.total,
        groundLink.mission.completed + 1) : 0
    readonly property bool routeMatches: confirmedRoute && uploadedLocalJson === localJson()
    readonly property bool canUpdateLocal: indoorMode && !groundLink.busy && updateMode &&
        firstLocalChange() >= lockedPrefixCount && (!continuationMode || localWaypoints.count > groundLink.mission.completed)
    readonly property bool canStartLocal: indoorMode && routeMatches && groundLink.remoteAvailable && !groundLink.busy &&
        groundLink.mission.state === "READY" && groundLink.mission.flight_ready && reviewedSession === groundLink.mission.map_session_id &&
        uploadedMissionId === groundLink.mission.mission_id && uploadedMissionRevision === groundLink.mission.revision
    Settings { id: localStore; category: "IndoorMissionV1" }
    ListModel { id: localWaypoints }
    property real defaultLocalZ: {
        var v = Number(localStore.value("defaultZ", 1.0))
        return (!isNaN(v) && v >= 0 && v <= 100) ? v : 1.0
    }

    function missionLabel() {
        if (!groundLink.remoteAvailable) return "等待机载任务连接"
        if (groundLink.mission.reason.indexOf("Returning to takeoff") === 0) return "正在返航"
        if (groundLink.mission.reason.indexOf("Returned to takeoff") === 0) return "已返航，悬停中"
        var labels = {IDLE: "尚无机载任务", READY: "已上传，等待开始", FLYING: "前往航点", HOVERING: "到点悬停", COMPLETED: "任务完成", FAILED: "任务异常", LANDING: "降落中"}
        return labels[groundLink.mission.state] || "状态未知"
    }
    function localJson() {
        var revision = localRevision // Track ListModel edits in dependent bindings.
        var points = []
        for (var i = 0; i < localWaypoints.count; ++i) {
            var p = localWaypoints.get(i)
            points.push({id: i + 1, x: Number(p.x), y: Number(p.y), z: Number(p.z), hover_sec: Number(p.hover_sec)})
        }
        return JSON.stringify(points)
    }
    function validLocal(x, y, z, hold) {
        return isFinite(x) && isFinite(y) && isFinite(z) && isFinite(hold) && Math.abs(x) <= 1000 && Math.abs(y) <= 1000 && z >= 0 && z <= 100 && hold >= 0 && hold <= 60
    }
    function addLocal(x, y, z) {
        var alt = (z !== undefined && isFinite(z)) ? z : ((typeof defaultLocalZ !== "undefined" && isFinite(defaultLocalZ)) ? defaultLocalZ : 1.0)
        if (!localEditable || localWaypoints.count >= 200 || !validLocal(x, y, alt, 2)) return
        localWaypoints.append({x: Number(x.toFixed(2)), y: Number(y.toFixed(2)), z: Number(alt.toFixed(2)), hover_sec: 2.0})
        localSelected = localWaypoints.count - 1; localRevision++
    }
    function moveLocal(index, x, y) {
        if (!canEditLocal(index) || !validLocal(x, y, 1, 2)) return
        localWaypoints.setProperty(index, "x", Number(x.toFixed(2)))
        localWaypoints.setProperty(index, "y", Number(y.toFixed(2)))
        localSelected = index; localRevision++
    }
    function canEditLocal(index) {
        return localEditable && index >= lockedPrefixCount && index >= 0 && index < localWaypoints.count
    }
    function removeLocal(index) {
        if (!canEditLocal(index)) return
        localWaypoints.remove(index)
        localSelected = Math.min(localSelected, localWaypoints.count - 1); localRevision++
    }
    function firstLocalChange() {
        if (!uploadedLocalJson.length) return -1
        var previous = JSON.parse(uploadedLocalJson), draft = JSON.parse(localJson())
        for (var i = 0; i < Math.max(previous.length, draft.length); ++i)
            if (JSON.stringify(previous[i]) !== JSON.stringify(draft[i])) return i
        return -1
    }
    function discardLocalChanges() {
        if (!confirmedRoute || groundLink.busy) return
        var points = JSON.parse(uploadedLocalJson)
        localWaypoints.clear()
        for (var i = 0; i < points.length; ++i) {
            var p = points[i]; localWaypoints.append({x: p.x, y: p.y, z: p.z, hover_sec: p.hover_sec})
        }
        localSelected = Math.min(localSelected, localWaypoints.count - 1); localRevision++
    }
    function localDraftStatus() {
        if (!groundLink.remoteAvailable) return "等待机载连接；本地编辑尚未发送"
        if (landingLocked) return "降落锁定：可删除、清空或编辑本地航线；落地未解锁后复位机载任务，再重新核对上传"
        if (pendingUpdateRequest.length) return "更新已发送，等待机载确认；确认后以机载状态为准"
        if (missionActive && !confirmedRoute) return "仅查看机载进度；本地航线版本尚未确认，不能更新"
        var first = firstLocalChange()
        if ((missionActive || continuationMode) && first >= 0 && first < lockedPrefixCount)
            return "草稿中的航点已进入执行，请先撤销草稿，再编辑后续航点"
        if (continuationMode) return first >= 0 ? "已完成航点锁定；点击追加后续航点，确认后再开始续飞" : groundLink.mission.state === "READY" ? "追加已确认；点击开始续飞，仅执行未完成航点" : "任务已完成；可追加航点，确认后再开始续飞"
        if (missionActive) return first >= 0 ? "后续航点已修改，点击更新后续航点后生效" : "已完成及当前执行点锁定，可编辑或追加后续航点"
        return routeMatches ? "当前航线已获机载确认" : "本地草稿；上传并获得机载确认后生效"
    }
    function storeLocalRoute(json) {
        localStore.setValue("routeJson", json); localStore.sync()
    }
    function saveLocal() {
        storeLocalRoute(localJson())
        actionMessage("局部航线已保存；定位重启后需重新核对坐标")
    }
    function clearLocalRoute() {
        if (!localReplaceable) return
        if (continuationMode) clearConfirmedRoute()
        localWaypoints.clear(); localSelected = -1; localRevision++
    }
    function importLocal(text) {
        if (!localReplaceable) return
        try {
            var points = JSON.parse(text)
            if (!Array.isArray(points) || points.length > 200) throw new Error("航点数量应为 0–200")
            for (var i = 0; i < points.length; ++i) {
                var p = points[i]
                for (var k = 0; k < 4; ++k) {
                    var key = ["x", "y", "z", "hover_sec"][k]
                    if (typeof p[key] !== "number") throw new Error("需要数值字段 x/y/z/hover_sec")
                }
                if (!validLocal(p.x, p.y, p.z, p.hover_sec)) throw new Error("局部坐标或悬停时间无效")
            }
            if (continuationMode) clearConfirmedRoute()
            localWaypoints.clear()
            for (i = 0; i < points.length; ++i) {
                p = points[i]; localWaypoints.append({x: p.x, y: p.y, z: p.z, hover_sec: p.hover_sec})
            }
            localSelected = localWaypoints.count ? 0 : -1; localRevision++
            routeDialog.close()
        } catch (error) { actionMessage("局部航线导入失败：" + error.message) }
    }
    function uploadLocal() {
        if (!localReplaceable || missionActive || continuationMode || !groundLink.remoteAvailable || reviewedSession !== groundLink.mission.map_session_id) {
            actionMessage("请连接机载任务网关，核对本次定位会话的原点和方向，再勾选确认")
            return
        }
        pendingUploadJson = localJson()
        pendingUploadSession = reviewedSession
        pendingUploadRequest = groundLink.uploadLocalRoute(JSON.parse(pendingUploadJson), reviewedSession)
    }
    function updateLocal() {
        if (!canUpdateLocal) { actionMessage("请核对定位会话，仅修改尚未执行的后续航点；有草稿冲突时先撤销草稿"); return }
        var replaceFrom = firstLocalChange()
        pendingUpdateJson = localJson()
        pendingUpdateMissionId = uploadedMissionId
        pendingUpdateSession = uploadedSession
        pendingUpdateRevision = uploadedMissionRevision + 1
        pendingUpdateRequest = groundLink.updateLocalRoute(JSON.parse(pendingUpdateJson).slice(replaceFrom),
            replaceFrom, uploadedMissionId, pendingUpdateRevision, reviewedSession)
    }
    function startLocal() {
        if (!canStartLocal) { actionMessage("请上传当前航线，并等待机载起飞悬停及任务就绪"); return }
        groundLink.startUploadedMission(groundLink.mission.mission_id, groundLink.mission.revision, reviewedSession)
    }
    function returnLocal() {
        if (!canReturnLocal) return
        groundLink.returnToHome(groundLink.mission.mission_id, groundLink.mission.revision, groundLink.mission.map_session_id)
    }
    function localPointState(index) {
        if (landingLocked) return "本地草稿"
        if (!confirmedRoute) return "本地编辑"
        var draft = JSON.parse(localJson()), previous = JSON.parse(uploadedLocalJson)
        if (JSON.stringify(draft[index]) !== JSON.stringify(previous[index])) return index < lockedPrefixCount ? "草稿冲突" : "待更新"
        if (index < groundLink.mission.completed) return "已完成"
        if (groundLink.mission.state === "FAILED") return "任务异常"
        if (missionActive && index + 1 === groundLink.mission.current_waypoint_id) return groundLink.mission.state === "HOVERING" ? "到点悬停" : "执行中"
        return "待执行"
    }
    function clearConfirmedRoute() {
        uploadedLocalJson = ""; uploadedMissionId = ""; uploadedMissionRevision = 0; uploadedSession = ""
    }
    function handleMissionChanged() {
        if (!groundLink.remoteAvailable) { reviewedSession = ""; return }
        if (reviewedSession !== groundLink.mission.map_session_id) reviewedSession = ""
        // changed precedes commandFinished for ACKs. Preserve the baseline until the matching update ACK arrives.
        var awaitingRevision = pendingUpdateRequest.length && pendingUpdateSession === groundLink.mission.map_session_id &&
            pendingUpdateMissionId === groundLink.mission.mission_id && pendingUpdateRevision === groundLink.mission.revision
        if (uploadedMissionId.length && !confirmedRoute && !awaitingRevision) clearConfirmedRoute()
    }
    function finishLocalCommand(requestId, operation, accepted) {
        if (operation === "upload" && requestId === pendingUploadRequest) {
            if (accepted && groundLink.remoteAvailable && pendingUploadSession === groundLink.mission.map_session_id && groundLink.mission.revision === 1) {
                uploadedLocalJson = pendingUploadJson; uploadedMissionId = groundLink.mission.mission_id
                uploadedMissionRevision = groundLink.mission.revision; uploadedSession = pendingUploadSession
                storeLocalRoute(pendingUploadJson)
            } else clearConfirmedRoute()
            pendingUploadRequest = ""
        }
        if (operation === "update" && requestId === pendingUpdateRequest) {
            if (accepted && groundLink.remoteAvailable && pendingUpdateSession === groundLink.mission.map_session_id &&
                    pendingUpdateMissionId === groundLink.mission.mission_id && pendingUpdateRevision === groundLink.mission.revision) {
                uploadedLocalJson = pendingUpdateJson; uploadedMissionId = pendingUpdateMissionId
                uploadedMissionRevision = pendingUpdateRevision; uploadedSession = pendingUpdateSession
                storeLocalRoute(pendingUpdateJson)
            }
            pendingUpdateRequest = ""
            handleMissionChanged()
        }
    }
    Connections {
        target: groundLink
        function onNotified(text) { page.actionMessage(text) }
        function onChanged() { page.handleMissionChanged() }
        function onCommandFinished(requestId, operation, accepted, reason) {
            page.finishLocalCommand(requestId, operation, accepted)
            var text = (accepted ? "机载已确认 " : "机载未确认 ") + operation + " · " + reason
            page.actionMessage(text)
            liveLogs.insert(0, {time: Qt.formatTime(new Date(), "hh:mm:ss"), type: "任务", module: "机载", content: text})
        }
    }
    property bool nestOpen: false
    property bool charging: true
    property bool thermal: false
    // 云台指令目标值（滑条用；真实角度以 gimbalLink 回报为准）
    property real pitchCmd: 0
    property real headingCmd: 0
    property real zoomCmd: 1
    property int selectedWaypoint: -1
    property int routeRevision: 0
    property int selectedVideo: 0
    property int photoPage: 0
    property int inspectedPhoto: 0
    signal requestSettings()
    Settings { id: routeStore; category: "MissionDemo" }

    // ── 云台链路 ──────────────────────────────────────────────
    // 相机只对机载电脑可达，实际连的是机载电脑上的 UDP 中转。
    // 中转与视频中转（MediaMTX）跑在同一台机器上，所以主机名从视频地址推导
    // （统一放在 VideoConfig.relayHost，避免两处各写一份正则）。
    readonly property string gimbalHost: VideoConfig.relayHost
    readonly property int gimbalPort: VideoConfig.gimbalPort

    function connectGimbal() {
        gimbalLink.connectToGimbal(gimbalHost, gimbalPort)
        // 照片库 / 录像库走同一台机载电脑上的 HTTP 代理（中转服务的 37261 端口）
        photoLibrary.setEndpoint(gimbalHost, VideoConfig.photoPort)
        videoLibrary.setEndpoint(gimbalHost, VideoConfig.photoPort)
        // 若当前正停在「视频列表」页，补一次刷新：
        // onContentTabChanged 可能早于本函数执行，那时还没地址，刷新会直接返回。
        if (contentTab === 3)
            Qt.callLater(function() { videoLibrary.refresh() })
        actionMessage("云台链路：" + gimbalHost + ":" + gimbalPort)
    }

    Connections {
        target: gimbalLink
        function onNotified(text) { page.actionMessage(text) }
        // 拍照成功 → 相机写完文件后自动取回最新一张，立即出现在图片列表
        function onPhotoSucceeded() {
            photoResyncTimer.restart()
        }
        // 拍照失败（例如没插 TF 卡）就不用去取图了
        function onPhotoFailed(reason) {
            photoResyncTimer.stop()
        }
        // 录像状态变化：1=录像中，0=停止。停止后相机需要时间写文件，
        // 稍等再刷新录像列表，新录的文件就会自动出现在「视频列表」里。
        function onRecordStatusChanged() {
            if (gimbalLink.recordStatus === 0 && wasRecording) {
                wasRecording = false
                videoResyncTimer.restart()
            } else if (gimbalLink.recordStatus === 1) {
                wasRecording = true
            }
        }
    }

    // 是否刚在录像（用于识别"停止录像"这个边沿，避免重复刷新）
    property bool wasRecording: false

    Timer {
        // 相机写完文件需要一点时间（实测约 1~2 秒），稍等再同步
        id: photoResyncTimer
        interval: 1800
        repeat: false
        onTriggered: photoLibrary.fetchNewest()
    }

    Timer {
        // 录像文件比照片大，多等一点再刷新列表
        id: videoResyncTimer
        interval: 2500
        repeat: false
        onTriggered: {
            videoLibrary.refresh()
            page.actionMessage("录像已结束，正在刷新视频列表…")
        }
    }

    Connections {
        target: photoLibrary
        function onNotified(text) { page.actionMessage(text) }
    }
    Connections {
        target: videoLibrary
        function onNotified(text) { page.actionMessage(text) }
    }

    // 机载电脑地址变化时（例如「自动发现」扫到了新地址）重连云台与照片库
    Connections {
        target: VideoConfig
        function onHostChanged() { page.connectGimbal() }
    }

    // 切到「视频列表」时按需刷新录像列表（录像列表不在启动时拉，省流量）
    onContentTabChanged: {
        if (contentTab === 3 && !videoLibrary.busy
                && videoLibrary.endpoint.length > 0)
            Qt.callLater(function() { videoLibrary.refresh() })
    }

    // 录像列表：取当前选中项
    // ⚠️ QML 陷阱：`property var x: someFunction()` 这种绑定**不建立依赖**，
    //    只在创建时求值一次（那时列表还是空的）→ 永远拿到 null。
    //    所以这里改成"显式刷新到页面属性"的方式，由数据和选中变化驱动。
    property var videoSelection: null

    function refreshVideoSelection() {
        var list = videoLibrary.photos
        if (!list || selectedVideo < 0 || selectedVideo >= list.length) {
            videoSelection = null
            return
        }
        videoSelection = list[selectedVideo]
    }
    onSelectedVideoChanged: refreshVideoSelection()

    Connections {
        target: videoLibrary
        function onPhotosChanged() { page.refreshVideoSelection() }
    }

    function selectedVideoEntry() {
        refreshVideoSelection()
        return videoSelection
    }

    function routeJson() {        var rows = []
        for (var i=0; i<waypoints.count; i++) rows.push(waypoints.get(i))
        return JSON.stringify(rows, null, 2)
    }
    function savedRouteJson() { return routeStore.value("routeJson", "") }
    function saveRoute() { routeStore.setValue("routeJson", routeJson()); routeStore.sync(); actionMessage("航线已保存到本机配置") }
    Component.onCompleted: {
        var saved = savedRouteJson()
        if (saved.length) importRoute(saved)
        var localSaved = localStore.value("routeJson", "")
        if (localSaved.length) importLocal(localSaved)
        contentTab = startupTab
        // 面板就绪后自动连接云台中转（地址由视频流地址推导）
        Qt.callLater(connectGimbal)
        // 启动时后台同步一次照片列表（相机里已有的照片会逐步缓存到本机）
        Qt.callLater(function() { photoLibrary.refresh() })
    }
    function validCoordinate(lat, lon) {
        return isFinite(lat) && isFinite(lon) && Math.abs(lat) <= 85.05112878 && Math.abs(lon) <= 180
    }
    function addWaypoint(lat, lon) {
        if (!validCoordinate(lat, lon) || waypoints.count >= 200) { actionMessage("坐标无效或已达到 200 个航点上限"); return false }
        waypoints.append({no: String(waypoints.count + 1), lat: Number(lat).toFixed(7), lon: Number(lon).toFixed(7), alt: "120", speed: "8", action: "拍照", hover: "2", state: "待执行"})
        selectedWaypoint = waypoints.count - 1; routeRevision++
        return true
    }
    function moveWaypoint(index, lat, lon) {
        if (index < 0 || index >= waypoints.count || !validCoordinate(lat, lon)) return false
        waypoints.setProperty(index, "lat", Number(lat).toFixed(7))
        waypoints.setProperty(index, "lon", Number(lon).toFixed(7))
        selectedWaypoint = index; routeRevision++
        return true
    }
    function deleteWaypoint(index) {
        if (index < 0 || index >= waypoints.count) return
        waypoints.remove(index)
        for (var i = 0; i < waypoints.count; ++i) waypoints.setProperty(i, "no", String(i + 1))
        selectedWaypoint = Math.min(selectedWaypoint, waypoints.count - 1); routeRevision++
    }
    function clearRoute() { waypoints.clear(); selectedWaypoint = -1; routeRevision++ }
    function importRoute(jsonText) {
        try {
            var rows = JSON.parse(jsonText === undefined ? routeInput.text : jsonText)
            if (!Array.isArray(rows) || rows.length > 200) throw new Error("航点数量应为0–200")
            for(var i=0;i<rows.length;i++) {
                var p = rows[i]
                if (p.lat === null || p.lon === null || p.lat === undefined || p.lon === undefined || String(p.lat).trim() === "" || String(p.lon).trim() === "" || !page.validCoordinate(Number(p.lat),Number(p.lon))) throw new Error("经纬度格式无效或超出 Web Mercator 纬度范围")
                if (p.alt !== undefined && (!isFinite(Number(p.alt)) || Number(p.alt)<0)) throw new Error("高度应为非负数")
            }
            waypoints.clear()
            for(i=0;i<rows.length;i++) {
                p=rows[i]
                waypoints.append({no:String(i+1), lat:String(p.lat), lon:String(p.lon), alt:String(p.alt === undefined ? 120 : p.alt), speed:String(p.speed === undefined ? 8 : p.speed), action:String(p.action||"拍照"), hover:String(p.hover === undefined ? 2 : p.hover), state:"待执行"})
            }
            selectedWaypoint = waypoints.count ? 0 : -1; routeRevision++
            routeDialog.close(); actionMessage("已导入本地航线；未上传飞控")
        } catch(e) { actionMessage("导入失败："+e.message) }
    }

    function actionMessage(message) {
        toastText.text = message
        toast.opacity = 1
        toastTimer.restart()
    }

    ListModel {
        id: waypoints
    }

    ListModel {
        id: liveLogs
        ListElement { time: "15:20:11"; type: "任务"; module: "系统"; content: "开始任务：电力巡检_20241226" }
        ListElement { time: "15:20:12"; type: "通信"; module: "机巢"; content: "机巢连接成功（IP：192.168.1.100）" }
        ListElement { time: "15:20:13"; type: "机巢"; module: "机巢"; content: "停止充电成功" }
        ListElement { time: "15:20:15"; type: "机巢"; module: "机巢"; content: "打开舱门" }
        ListElement { time: "15:20:17"; type: "飞行"; module: "无人机"; content: "解锁成功" }
        ListElement { time: "15:20:20"; type: "飞行"; module: "无人机"; content: "起飞成功，高度 20 m" }
        ListElement { time: "15:20:35"; type: "任务"; module: "无人机"; content: "开始执行航线，共 12 个航点" }
        ListElement { time: "15:22:10"; type: "航点"; module: "无人机"; content: "到达航点 1，开始拍照" }
        ListElement { time: "15:24:18"; type: "航点"; module: "无人机"; content: "到达航点 2，开始拍照" }
        ListElement { time: "15:26:35"; type: "航点"; module: "无人机"; content: "到达航点 3，开始拍照" }
    }

    Row {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Item {
            width: 270
            height: parent.height

            GCard {
                id: deviceCard
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                height: 342
                title: "设备状态"

                Column {
                    x: 10; y: 10; width: parent.width - 20; spacing: 7
                    Row {
                        width: parent.width; height: 98
                        Image { width: 98; height: 86; source: "qrc:/assets/drone.png"; fillMode: Image.PreserveAspectFit }
                        Column {
                            width: 146; spacing: 7
                            Row { width: parent.width; Text { objectName: "taskAircraftName"; text: aircraftProfile.name; color: "white"; font.pixelSize: 16; font.bold: true; width: 99; elide: Text.ElideRight } StatusDot { text: groundLink.connected && groundLink.fcuConnected ? "在线" : "未连接"; dotColor: groundLink.connected && groundLink.fcuConnected ? "#00db80" : "#8394a3" } }
                            Text { objectName: "taskAircraftModel"; text: "型号        " + aircraftProfile.model; color: "#cddcec"; font.pixelSize: 14; width: parent.width; elide: Text.ElideRight }
                            Text { objectName: "taskAutopilot"; text: "飞控        " + aircraftProfile.autopilot; color: "#cddcec"; font.pixelSize: 14; width: parent.width; elide: Text.ElideRight }
                            Row { spacing:4; Text { text: "电量        "; color: "#cddcec"; font.pixelSize: 14 } Text { text: groundLink.connected && groundLink.batteryPercent >= 0 ? Math.round(groundLink.batteryPercent) + "%" : "—"; color: "#00e787"; font.pixelSize: 14; font.bold: true } Rectangle { width:42; height:11; radius:2; color:"#122d41"; anchors.verticalCenter:parent.verticalCenter; Rectangle {width:parent.width * Math.max(0, groundLink.batteryPercent) / 100; height:11; radius:2; color:"#00c477"} } }
                        }
                    }
                    Repeater {
                        model: [
                            ["定位", groundLink.odomFresh ? "world 坐标有效" : "未获得 world 定位"], ["飞行模式", groundLink.mode || "—"],
                            ["位置", groundLink.odomFresh ? groundLink.worldX.toFixed(1) + ", " + groundLink.worldY.toFixed(1) + " m" : "—"], ["高度", groundLink.odomFresh ? groundLink.worldZ.toFixed(1) + " m (world)" : "—"],
                            ["速度", "—"], ["卫星数", "—"], ["状态", groundLink.missionReady ? "机载任务就绪" : "未就绪"]
                        ]
                        delegate: Row {
                            width: 248; height: 21
                            Text { width: 102; text: modelData[0]; color: "#aebfd0"; font.pixelSize: 13 }
                            Text { text: modelData[1]; color: index === 6 ? "#00e787" : "#dce9f7"; font.pixelSize: index === 2 ? 11 : 13; font.bold: index === 6 }
                        }
                    }
                }
            }

            GCard {
                id: nestSummary
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: deviceCard.bottom; anchors.topMargin: 10
                height: 227
                title: "机巢"
                Row {
                    x: 10; y: 8; width: parent.width - 20
                    Image { width: 105; height: 98; source: "qrc:/assets/nest.png"; fillMode: Image.PreserveAspectFit }
                    Column {
                        width: 135; spacing: 6
                        Row { Text { text: "机巢"; color: "white"; font.pixelSize: 16; font.bold: true; width: 82 } StatusDot { text: "演示"; dotColor: "#8394a3"; textColor: "#9aafc0" } }
                        Text { text: "机巢型号    HIVE-01"; color: "#cddcec"; font.pixelSize: 13 }
                        Text { text: "舱门状态    " + (page.nestOpen ? "已打开" : "已关闭"); color: "#00e787"; font.pixelSize: 13 }
                        Text { text: "无人机      机内"; color: "#00e787"; font.pixelSize: 13 }
                        Text { text: "充电状态    " + (page.charging ? "充电中" : "已停止"); color: "#00e787"; font.pixelSize: 13 }
                        Text { text: "环境温度    23.5 ℃"; color: "#cddcec"; font.pixelSize: 13 }
                        Text { text: "环境湿度    45 %"; color: "#cddcec"; font.pixelSize: 13 }
                    }
                }
            }

            GCard {
                anchors.left: parent.left; anchors.right: parent.right; anchors.topMargin: 10
                anchors.top: nestSummary.bottom
                anchors.bottom: parent.bottom
                title: page.indoorMode ? "室内航点任务" : "任务信息（本地编辑）"
                Column {
                    x: 10; y: 8; width: parent.width - 20; spacing: 5
                    Repeater {
                        model: [["任务名称", page.indoorMode ? "室内演示" : "室外航线"], ["任务类型", page.indoorMode ? "局部坐标 · world" : "WGS84 本地编辑"], ["任务状态", page.missionState], ["定位状态", groundLink.odomFresh ? "有效" : "等待定位"], ["任务就绪", groundLink.missionReady ? "就绪" : "未就绪"]]
                        delegate: Row {
                            width: parent.width; height: 20
                            Text { width: 84; text: modelData[0]; color: "#aebfd0"; font.pixelSize: 13 }
                            Text { text: modelData[1]; color: index === 2 ? "#ffffff" : "#dce9f7"; font.pixelSize: 13 }
                        }
                    }
                    Row { width: parent.width; Text { text: "航点进度"; width: 84; color: "#aebfd0"; font.pixelSize: 13 } Text { text: groundLink.remoteAvailable ? groundLink.mission.completed + " / " + groundLink.mission.total : "—"; width: 120; color: "#dce9f7"; font.pixelSize: 13 } Text { width: 44; horizontalAlignment: Text.AlignRight; text: groundLink.remoteAvailable && groundLink.mission.total > 0 ? Math.round(groundLink.mission.completed / groundLink.mission.total * 100) + "%" : "—"; color: "#dce9f7"; font.pixelSize: 13 } }
                    Rectangle { width: parent.width; height: 13; radius: 6; color: "#143149"; Rectangle { width: groundLink.remoteAvailable && groundLink.mission.total > 0 ? parent.width * groundLink.mission.completed / groundLink.mission.total : 0; height: parent.height; radius: 6; color: "#087dff" } }
                    Row { width: parent.width; Text { text: "机载版本"; width: 84; color: "#aebfd0"; font.pixelSize: 13 } Text { text: groundLink.remoteAvailable ? "v" + groundLink.mission.revision + " · 当前航点 " + (groundLink.mission.current_waypoint_id || "—") : "—"; color: "#dce9f7"; font.pixelSize: 13 } }
                    Grid {
                        width: parent.width; columns: 2; spacing: 8
                        GButton { width: 119; height:36; text: groundLink.mission.completed > 0 && groundLink.mission.state === "READY" ? "开始续飞" : "开始任务"; icon: "▶"; fill: "#00b968"; hoverFill: "#00d079"; enabled: page.canStartLocal; onClicked: page.startLocal() }
                        GButton { width: 119; height:36; text: "暂停任务"; icon: "Ⅱ"; fill: "#ff8a00"; hoverFill: "#ffa126"; enabled: false; onClicked: page.actionMessage("首版暂不支持暂停，请使用遥控器接管") }
                        GButton { width: 119; height:36; text: "返航"; icon: "⌾"; fill: "#087dff"; hoverFill: "#1695ff"; enabled: page.canReturnLocal; onClicked: page.returnLocal() }
                        GButton { width: 119; height:36; text: "紧急降落"; icon: "×"; fill: "#ff4148"; hoverFill: "#ff5c61"; enabled: false }
                    }
                }
            }
        }

        Item {
            width: 810
            height: parent.height
                MapPanel {
                    id: map; objectName: "taskMapPanel"
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    // Only the lower list changes tabs; keep the map viewport fixed.
                    height: 450
                    visible: !page.indoorMode
                    editable: true; waypointModel: waypoints
                    selectedIndex: page.selectedWaypoint; routeRevision: page.routeRevision
                    onWaypointAdded: page.addWaypoint(latitude, longitude)
                    onWaypointMoved: page.moveWaypoint(index, latitude, longitude)
                    onWaypointSelected: page.selectedWaypoint = index
                    onClearRequested: clearRouteDialog.open()
                }

            IndoorMapPanel {
                id: indoorMap
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                height: 450; visible: page.indoorMode
                waypointModel: localWaypoints; selectedIndex: page.localSelected; routeRevision: page.localRevision
                editingEnabled: page.localEditable
                wholeRouteEditable: page.localReplaceable
                lockedPrefixCount: page.lockedPrefixCount
                completedCount: page.confirmedRoute ? groundLink.mission.completed : 0
                activeIndex: page.confirmedRoute && page.missionActive ? groundLink.mission.completed : -1
                defaultZ: page.defaultLocalZ
                onDefaultZChanged: {
                    page.defaultLocalZ = defaultZ
                    localStore.setValue("defaultZ", defaultZ)
                }
                onWaypointAdded: page.addLocal(xMeters, yMeters, zMeters)
                onWaypointMoved: page.moveLocal(index, xMeters, yMeters)
                onWaypointSelected: page.localSelected = index
                onWaypointDoubleClicked: {
                    if (!page.canEditLocal(index)) return
                    page.localSelected = index
                    var pt = localWaypoints.get(index)
                    localX.text = pt.x; localY.text = pt.y; localZ.text = pt.z; localHold.text = pt.hover_sec
                    localEdit.open()
                }
                onClearRequested: clearRouteDialog.open()
                onOutdoorRequested: page.indoorMode = false
            }
            GButton {
                visible: !page.indoorMode; x: 10; y: 54; width: 122; height: 30
                text: "切到室内地图"; onClicked: page.indoorMode = true
            }

            GCard {
                objectName: "taskDataPanel"
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: map.bottom; anchors.topMargin: 8; anchors.bottom: parent.bottom
                Item {
                    id: tabs
                    objectName: "taskContentTabs"
                    width: parent.width; height: 46
                    Row {
                        anchors.left: parent.left
                        Repeater {
                            model: ["航点列表", "实时日志", "图片列表", "视频列表"]
                            delegate: Rectangle {
                                objectName: "taskContentTab" + index
                                width: 120; height: 46
                                color: page.contentTab === index ? "#087dff" : "#071a29"
                                border.color: "#14344d"
                                Text { anchors.centerIn: parent; text: modelData; color: page.contentTab === index ? "white" : "#b9cadb"; font.pixelSize: 15; font.bold: page.contentTab === index }
                                MouseArea { anchors.fill: parent; onClicked: page.contentTab = index }
                            }
                        }
                    }
                    Row {
                        anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; spacing: 8
                        GButton { visible: page.contentTab === 0; width: 92; height: 34; text: "导入航线"; enabled: !page.indoorMode || page.localReplaceable; onClicked: { routeInput.text = page.indoorMode ? page.localJson() : page.savedRouteJson() || page.routeJson(); routeDialog.open() } }
                        GButton { visible: page.contentTab === 0; width: 92; height: 34; text: "保存航线"; onClicked: page.indoorMode ? page.saveLocal() : page.saveRoute() }
                        GButton { objectName: "localRouteSendButton"; visible: page.contentTab === 0; width: page.updateMode || page.missionActive ? 112 : 92; height: 34
                            text: page.landingLocked ? "降落锁定" : page.continuationMode ? "追加后续航点" : page.missionActive ? "更新后续航点" : "上传航线"
                            enabled: !page.landingLocked && (page.missionActive || page.updateMode ? page.canUpdateLocal : page.indoorMode && page.localReplaceable && groundLink.remoteAvailable)
                            onClicked: page.missionActive || page.updateMode ? page.updateLocal() : page.uploadLocal()
                        }
                    }
                }
                Loader { anchors.left: parent.left; anchors.right: parent.right; anchors.top: tabs.bottom; anchors.bottom: parent.bottom; sourceComponent: page.contentTab === 0 ? (page.indoorMode ? localWaypointView : waypointView) : page.contentTab === 1 ? logView : page.contentTab === 2 ? photoView : videoView }
            }
        }

        Item {
            width: 410
            height: parent.height
            GCard {
                id: realtime
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                height: 385
                title: "实时视频"
                Row {
                    anchors.right: parent.right; anchors.rightMargin: 8; y: -37; height: 36
                    GButton { width: 78; height: 34; text: "主相机"; fill: page.thermal ? "transparent" : "#087dff"; onClicked: page.thermal=false }
                    GButton { width: 70; height: 34; text: "热成像"; fill: page.thermal ? "#087dff" : "transparent"; onClicked: {page.thermal=true; page.actionMessage("热成像示意，未接入热成像视频源")} }
                }
                VideoPanel { anchors.fill: parent; anchors.margins: 5; thermal: page.thermal }
            }

            GCard {
                id: gimbal
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: realtime.bottom; anchors.topMargin: 10
                height: 268
                title: "云台控制"

                // ── 状态与重连 ──
                Row {
                    anchors.right: parent.right; anchors.rightMargin: 10; y: -35; height: 30; spacing: 10
                    StatusDot {
                        anchors.verticalCenter: parent.verticalCenter
                        text: gimbalLink.connected ? "云台在线" : "云台离线"
                        dotColor: gimbalLink.connected ? "#00db80" : "#8394a3"
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        color: gimbalLink.recordStatus === 1 ? "#ff6b6b" : "#9fb4c6"; font.pixelSize: 12
                        text: gimbalLink.recordStatusName + " · " + gimbalLink.modeName
                    }
                    // 相机没插 TF 卡时拍照/录像都会失败，必须显眼提示
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !gimbalLink.tfCardPresent
                        color: "#ff9a3c"; font.pixelSize: 12; font.bold: true
                        text: "⚠ " + gimbalLink.tfCardText
                    }
                    GButton { width: 56; height: 28; text: "重连"; onClicked: page.connectGimbal() }
                }

                Row {
                    x: 11; y: 8; spacing: 20
                    // ── 360° 拨杆：杆量 → 云台转向速度（SDK 0x07）──
                    Item {
                        id: joy
                        width: 106; height: 106
                        readonly property real throwRadius: 34
                        readonly property real knobRadius: 13
                        readonly property real deadZone: 0.06      // 中心死区，避免手抖
                                                                   // （原 0.10：小幅度推杆
                                                                   //   直接归零，手感"推了不动"）
                        property real nx: 0                        // -1..1，右为正
                        property real ny: 0                        // -1..1，上为正
                        property bool dragging: false
                        readonly property int yawRate:
                            Math.abs(nx) < deadZone ? 0 : Math.round(nx * 100)
                        readonly property int pitchRate:
                            Math.abs(ny) < deadZone ? 0 : Math.round(ny * 100)

                        function apply(px, py) {
                            var dx = px - width / 2
                            var dy = py - height / 2
                            var dist = Math.sqrt(dx * dx + dy * dy)
                            if (dist > throwRadius) {
                                dx = dx / dist * throwRadius
                                dy = dy / dist * throwRadius
                            }
                            nx = dx / throwRadius
                            ny = -dy / throwRadius                 // 屏幕向下为正，取反
                            gimbalLink.setRotateRate(yawRate, pitchRate)
                        }
                        function release() {
                            dragging = false
                            nx = 0
                            ny = 0
                            gimbalLink.stopRotate()                // 松手必须发 0
                        }

                        // 底盘
                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            color: "#04121d"
                            border.color: joy.dragging ? "#0f7fd8" : "#123650"
                            border.width: 2
                        }
                        // 中心十字参考
                        Rectangle { anchors.centerIn: parent; width: 1; height: parent.height * 0.42; color: "#123a55" }
                        Rectangle { anchors.centerIn: parent; width: parent.width * 0.42; height: 1; color: "#123a55" }
                        // 死区圈
                        Rectangle {
                            anchors.centerIn: parent
                            width: joy.throwRadius * 2 * joy.deadZone
                            height: width; radius: width / 2
                            color: "transparent"; border.color: "#16405e"
                        }
                        // 杆量指示线（从中心指向拨杆头）
                        Rectangle {
                            visible: joy.nx !== 0 || joy.ny !== 0
                            width: 2
                            height: Math.sqrt(joy.nx * joy.nx + joy.ny * joy.ny) * joy.throwRadius
                            color: "#087dff"
                            x: joy.width / 2 - 1
                            y: joy.height / 2 - height
                            transformOrigin: Item.Bottom
                            rotation: Math.atan2(joy.nx, joy.ny) * 180 / Math.PI
                        }
                        // 拨杆头
                        Rectangle {
                            width: joy.knobRadius * 2; height: width; radius: width / 2
                            color: joy.dragging ? "#087dff" : "#1c4a6b"
                            border.color: joy.dragging ? "#7fc0ff" : "#2d6d99"; border.width: 1
                            x: joy.width / 2 + joy.nx * joy.throwRadius - width / 2
                            y: joy.height / 2 - joy.ny * joy.throwRadius - height / 2
                            Behavior on x { NumberAnimation { duration: 60 } }
                            Behavior on y { NumberAnimation { duration: 60 } }
                            Text { anchors.centerIn: parent; text: "拨杆"; color: "#cfe0ef"; font.pixelSize: 9 }
                        }
                        // 杆量读数
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom; anchors.bottomMargin: 3
                            font.pixelSize: 9
                            color: (joy.yawRate || joy.pitchRate) ? "#00e787" : "#5b7488"
                            text: "偏航 " + (joy.yawRate > 0 ? "+" : "") + joy.yawRate
                                + "  俯仰 " + (joy.pitchRate > 0 ? "+" : "") + joy.pitchRate
                        }
                        MouseArea {
                            id: stickMouse
                            anchors.fill: parent
                            onPressed: { joy.dragging = true; joy.apply(mouseX, mouseY) }
                            onPositionChanged: if (joy.dragging) joy.apply(mouseX, mouseY)
                            onReleased: joy.release()
                            onCanceled: joy.release()
                        }
                        // 持续上报杆量（30Hz）：相机不会自己保持，需要流式下发。
                        // 杆量【变化】时 apply() 已经立即下发，这个定时器只做保活，
                        // 所以间隔不必太短；原来 50ms 在松手/静止时也无所谓。
                        Timer {
                            interval: 33; repeat: true; running: joy.dragging
                            onTriggered: gimbalLink.setRotateRate(joy.yawRate, joy.pitchRate)
                        }
                        // 安全兜底 1：丢过鼠标抬起事件时强制回中（按 Qt 的真实按键状态判断）
                        Timer {
                            interval: 300; repeat: true; running: joy.dragging
                            onTriggered: if (!stickMouse.pressed) joy.release()
                        }
                        // 安全兜底 2：窗口失去激活（切走/最小化）立即回中
                        Connections {
                            target: window
                            function onActiveChanged() { if (!window.active && joy.dragging) joy.release() }
                        }
                    }
                    // ── 滑条：绝对角度 / 变倍 ──
                    Column {
                        width: 240; spacing: 13
                        Repeater {
                            model: ["俯仰角", "航向角", "变焦"]
                            delegate: Row {
                                width: 252; height: 25; spacing: 5
                                Text { width: 53; text: modelData; color: "#aebfd0"; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                                Text {
                                    width: 38; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter
                                    text: index===0 ? gimbalLink.pitch.toFixed(0)+"°"
                                        : index===1 ? gimbalLink.yaw.toFixed(0)+"°"
                                        : gimbalLink.zoom.toFixed(1)+"x"
                                    color: gimbalLink.attitudeFresh ? "#dce9f7" : "#6d8496"
                                }
                                GButton {
                                    width:25; height:25; text:"−"; fill:"#091e2d"
                                    onClicked: {
                                        if (index===0) gimbalLink.jog(0, -1)
                                        else if (index===1) gimbalLink.jog(-1, 0)
                                        else gimbalLink.zoomStep(-1)
                                    }
                                }
                                Slider {
                                    id: adjuster
                                    width: 88; height:25
                                    from: index===0 ? gimbalLink.pitchMin : index===1 ? -gimbalLink.yawLimit : 1
                                    to:   index===0 ? gimbalLink.pitchMax : index===1 ?  gimbalLink.yawLimit : gimbalLink.zoomMax
                                    stepSize: index===2 ? 0.1 : 1
                                    value: index===0 ? page.pitchCmd : index===1 ? page.headingCmd : page.zoomCmd
                                    onMoved: {
                                        if (index===0) { page.pitchCmd = value; gimbalLink.setAttitude(page.headingCmd, value) }
                                        else if (index===1) { page.headingCmd = value; gimbalLink.setAttitude(value, page.pitchCmd) }
                                        else { page.zoomCmd = value; gimbalLink.setZoomAbsolute(value) }
                                    }
                                    background: Rectangle { x:adjuster.leftPadding; y:(adjuster.height-height)/2; width:adjuster.availableWidth; height:3; color:"#132f44"; Rectangle {width:adjuster.visualPosition*parent.width; height:3; color:"#0788ff"} }
                                    handle: Rectangle { x:adjuster.leftPadding+adjuster.visualPosition*(adjuster.availableWidth-width); y:(adjuster.height-height)/2; width:13; height:13; radius:7; color:"#087dff" }
                                }
                                GButton {
                                    width:25; height:25; text:"+"; fill:"#091e2d"
                                    onClicked: {
                                        if (index===0) gimbalLink.jog(0, 1)
                                        else if (index===1) gimbalLink.jog(1, 0)
                                        else gimbalLink.zoomStep(1)
                                    }
                                }
                                // 相机回报真实角度时同步滑条（用户正在拖动时不打扰）
                                Connections {
                                    target: gimbalLink
                                    function onAttitudeChanged() {
                                        if (!gimbalLink.attitudeFresh || adjuster.pressed) return
                                        if (index === 0) { adjuster.value = gimbalLink.pitch; page.pitchCmd = gimbalLink.pitch }
                                        else if (index === 1) { adjuster.value = gimbalLink.yaw; page.headingCmd = gimbalLink.yaw }
                                    }
                                    function onChanged() {
                                        if (index !== 2 || adjuster.pressed) return
                                        adjuster.value = gimbalLink.zoom
                                        page.zoomCmd = gimbalLink.zoom
                                    }
                                }
                            }
                        }
                    }
                }

                // ── 云台模式与快捷动作 ──
                Row {
                    x: 12; y: 122; spacing: 6
                    Repeater {
                        model: [["锁定", 0], ["跟随", 1], ["FPV", 2]]
                        delegate: GButton {
                            width: 58; height: 30; text: modelData[0]
                            fill: gimbalLink.mode === modelData[1] ? "#087dff" : "transparent"
                            onClicked: gimbalLink.setMode(modelData[1])
                        }
                    }
                    Item { width: 8; height: 1 }
                    GButton { width: 58; height: 30; text: "回中"; onClicked: gimbalLink.centerGimbal() }
                    GButton { width: 58; height: 30; text: "朝下"; onClicked: gimbalLink.lookDown() }
                    GButton { width: 58; height: 30; text: "软重启"; onClicked: gimbalLink.softReboot() }
                }

                // ── 相机功能 ──
                Row {
                    anchors.left: parent.left; anchors.leftMargin: 11; anchors.bottom: parent.bottom; anchors.bottomMargin: 11; spacing: 7
                    Repeater {
                        model: [["拍照", "◉"], ["录像", "■"], ["变焦+", "⌕"], ["变焦−", "⌕"]]
                        delegate: GButton {
                            width: 91; height: 38
                            text: index===1 && gimbalLink.recordStatus === 1 ? "停止" : modelData[0]
                            icon: modelData[1]
                            onClicked: {
                                if (index === 0) gimbalLink.takePhoto()
                                else if (index === 1) gimbalLink.toggleRecording()
                                else if (index === 2) gimbalLink.zoomStep(1)
                                else gimbalLink.zoomStep(-1)
                            }
                        }
                    }
                }
            }

            GCard {
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: gimbal.bottom; anchors.topMargin: 10; anchors.bottom: parent.bottom
                title: "机巢控制"
                Row {
                    x: 10; y: 10; spacing: 10
                    Column {
                        width: 145; spacing: 8
                        Image { width: 145; height: 125; source: "qrc:/assets/nest.png"; fillMode: Image.PreserveAspectFit }
                        Text { text: "机巢当前状态："; color: "#dce9f7"; font.pixelSize: 13 }
                        Text { text: page.nestOpen ? "已打开" : "已关闭"; color: "#00e787"; font.pixelSize: 14; font.bold: true }
                    }
                    Column {
                        width: 118; spacing: 8
                        GButton { width: 112; height: 36; text: "打开舱门"; fill: "#00af64"; onClicked: {page.nestOpen=true; page.actionMessage("演示舱门已打开，未发送机巢指令")} }
                        GButton { width: 112; height: 36; text: "关闭舱门"; onClicked: {page.nestOpen=false; page.actionMessage("演示舱门已关闭")} }
                        GButton { width: 112; height: 36; text: "开始充电"; onClicked: {page.charging=true; page.actionMessage("演示充电状态已开启")} }
                        GButton { width: 112; height: 36; text: "停止充电"; onClicked: {page.charging=false; page.actionMessage("演示充电状态已停止")} }
                        GButton { width: 112; height: 36; text: "机巢设置"; onClicked: page.requestSettings() }
                    }
                    Column {
                        spacing: 9
                        Text { text: "机巢状态"; color: "white"; font.pixelSize: 14; font.bold: true }
                        Repeater {
                            model: ["在线", page.nestOpen ? "舱门已打开" : "舱门已关闭", "无人机 机内", page.charging ? "充电中" : "已停止", "温度 23.5 ℃", "湿度 45 %", "供电 正常"]
                            delegate: Row { spacing: 8; Rectangle { width: 11; height: 11; radius: 6; color: "#00db80"; anchors.verticalCenter: parent.verticalCenter } Text { text: modelData; color: "#cfe0ef"; font.pixelSize: 12 } }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: localWaypointView
        Item {
            Row {
                x: 8; y: 3; spacing: 8
                CheckBox {
                    objectName: "localSessionReview"
                    checked: groundLink.remoteAvailable && page.reviewedSession === groundLink.mission.map_session_id
                    enabled: groundLink.remoteAvailable && !groundLink.busy
                    text: "已核对本次定位原点、X/Y方向和Z高度"
                    contentItem: Text { text: parent.text; color: "#c6dfeb"; leftPadding: parent.indicator.width + 6; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
                    onClicked: page.reviewedSession = checked ? groundLink.mission.map_session_id : ""
                }
                GButton { height: 32; width: 88; text: "查询机载"; enabled: groundLink.remoteAvailable && !groundLink.busy; onClicked: groundLink.queryMission() }
                Text { text: groundLink.remoteAvailable ? "Z范围 " + groundLink.mission.min_z + "–" + groundLink.mission.max_z + "m" : "连接设置 → 127.0.0.1:8765"; color: "#91adc3"; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
            }
            Rectangle {
                x: 0; y: 42; width: parent.width; height: 30; color: "#092337"
                Text { anchors.left: parent.left; anchors.leftMargin: 8; anchors.right: discardDraft.left; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                    text: page.localDraftStatus(); color: page.routeMatches ? "#8edbb9" : "#ffd18b"; font.pixelSize: 12; elide: Text.ElideRight
                }
                GButton { id: discardDraft; objectName: "localDiscardDraftButton"; anchors.right: parent.right; anchors.rightMargin: 8; height: 28; width: 88
                    text: "撤销草稿"; enabled: page.confirmedRoute && !page.routeMatches && !groundLink.busy; onClicked: page.discardLocalChanges()
                }
            }
            Rectangle {
                x: 0; y: 74; width: parent.width; height: 32; color: "#0a2234"
                Row {
                    Repeater { model: [["序号", 52], ["X (m)", 110], ["Y (m)", 110], ["Z 局部高度 (m)", 130], ["悬停 (s)", 100], ["状态", 120], ["操作", 150]]
                        delegate: Text { width: modelData[1]; height: 32; text: modelData[0]; color: "#9fb4c8"; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                }
            }
            ListView {
                objectName: "localWaypointList"
                x: 0; y: 106; width: parent.width; height: parent.height - y; model: localWaypoints; clip: true
                Text { anchors.centerIn: parent; visible: localWaypoints.count === 0; text: "点击添加航点，再在网格选点 · 默认 Z=" + page.defaultLocalZ.toFixed(1) + "m，可在上方或表格修改"; color: "#91adc3"; font.pixelSize: 13 }
                delegate: Rectangle {
                    id: localRow
                    // x/y are also Item geometry names: always access coordinates through model.
                    property real pointX: model.x
                    property real pointY: model.y
                    property real pointZ: model.z
                    property real pointHold: model.hover_sec
                    width: ListView.view.width; height: 35
                    color: index === page.localSelected ? "#0757ad" : index % 2 ? "#081d2d" : "#071725"
                    MouseArea { anchors.fill: parent; onClicked: page.localSelected = index }
                    Row {
                        Repeater {
                            model: [[index + 1, 52], [localRow.pointX.toFixed(2), 110], [localRow.pointY.toFixed(2), 110], [localRow.pointZ.toFixed(2), 130], [localRow.pointHold, 100], [page.localPointState(index), 120]]
                            delegate: Text { width: modelData[1]; height: 35; text: modelData[0]; color: "#dce9f7"; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        }
                        GButton { text: "编辑"; height: 31; width: 65; enabled: page.canEditLocal(index); onClicked: {
                            page.localSelected = index; localX.text = localRow.pointX; localY.text = localRow.pointY; localZ.text = localRow.pointZ; localHold.text = localRow.pointHold; localEdit.open()
                        } }
                        GButton { text: "删除"; height: 31; width: 65; enabled: page.canEditLocal(index); onClicked: page.removeLocal(index) }
                    }
                }
            }
        }
    }
    Dialog {
        id: localEdit
        title: "编辑局部航点 " + (page.localSelected + 1)
        width: 420; height: 320; x: (page.width - width) / 2; y: (page.height - height) / 2; modal: true
        background: Rectangle { color: "#0a2234"; border.color: "#1b5279"; radius: 6 }
        contentItem: Column {
            spacing: 12
            Row { Text { text: "X (m)"; width: 95; color: "white" } GField { id: localX; width: 270 } }
            Row { Text { text: "Y (m)"; width: 95; color: "white" } GField { id: localY; width: 270 } }
            Row { Text { text: "Z 局部高度"; width: 95; color: "white" } GField { id: localZ; width: 270 } }
            Row { Text { text: "悬停 (s)"; width: 95; color: "white" } GField { id: localHold; width: 270 } }
            GButton { text: "保存"; fill: "#087dff"; onClicked: {
                var px = Number(localX.text), py = Number(localY.text), pz = Number(localZ.text), hold = Number(localHold.text)
                if (!page.canEditLocal(page.localSelected) || !localX.text.trim().length || !localY.text.trim().length || !localZ.text.trim().length || !localHold.text.trim().length || !page.validLocal(px, py, pz, hold)) {
                    page.actionMessage("请输入有效的局部坐标和 0–60 秒悬停时间"); return
                }
                localWaypoints.set(page.localSelected, {x: px, y: py, z: pz, hover_sec: hold})
                page.localRevision++; localEdit.close()
            } }
        }
    }

    Component {
        id: waypointView
        Item {
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 36; color: "#0a2234"
                Row { anchors.fill: parent
                    Repeater { model: [["序号", 42], ["纬度", 108], ["经度", 116], ["高度 (m)", 80], ["速度 (m/s)", 94], ["动作", 68], ["悬停 (s)", 76], ["状态", 106], ["操作", 100]]; delegate: Text { width: modelData[1]; height: 36; text: modelData[0]; color: "#9fb4c8"; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter } }
                }
            }
            ListView {
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.topMargin: 36; anchors.bottom: parent.bottom
                model: waypoints; clip: true
                Text { anchors.centerIn: parent; visible: waypoints.count === 0; text: "暂无航点 · 点击地图上的“添加航点”开始规划"; color: "#91adc2"; font.pixelSize: 15 }
                delegate: Rectangle {
                    id: waypointRow
                    property string waypointState: model.state
                    width: ListView.view.width; height: 31; color: index === page.selectedWaypoint ? "#0757ad" : (index % 2 ? "#081d2d" : "#071725"); border.color: "#102d42"
                    MouseArea { anchors.fill:parent; onClicked:page.selectedWaypoint=index }
                    Row { anchors.fill: parent
                        Repeater { model: [[no, 42], [lat, 108], [lon, 116], [alt, 80], [speed, 94], [action, 68], [hover, 76], [waypointRow.waypointState, 106]]; delegate: Text { width: modelData[1]; height: 31; text: modelData[0]; color: modelData[0] === "已完成" ? "#00e787" : modelData[0] === "执行中" ? "#ffe000" : "#dce9f7"; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter } }
                        Row { width:100; height:31; spacing:8
                            GButton { text:"✎"; width:36; height:30; fill:"transparent"; onClicked:{ page.selectedWaypoint=index; editLat.text=lat; editLon.text=lon; editAlt.text=alt; waypointDialog.open() } }
                            GButton { text:"×"; width:36; height:30; fill:"transparent"; onClicked:page.deleteWaypoint(index) }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: logView
        Item {
            Row { anchors.right: parent.right; anchors.rightMargin: 10; y: 8; spacing: 8
                GButton { width: 75; height: 32; text: "清空"; onClicked: liveLogs.clear() }
                GButton { width: 92; height: 32; text: "导出日志"; onClicked: page.actionMessage("当前为示例日志，尚未接入文件导出") }
            }
            Rectangle { x: 7; y: 49; width: parent.width - 14; height: 34; color: "#0a2234"
                Row { anchors.fill: parent; Repeater { model: [["时间", 130], ["类型", 130], ["模块", 130], ["内容", 380]]; delegate: Text { width: modelData[1]; height: 34; text: modelData[0]; color: "#aabed1"; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter } } }
            }
            ListView { x: 7; y: 83; width: parent.width - 14; height: parent.height - 90; model: liveLogs; clip: true
                delegate: Rectangle { width: ListView.view.width; height: 29; color: index % 2 ? "#081d2d" : "#071725"; border.color: "#102d42"
                    Row { anchors.fill: parent
                        Rectangle { width: 12; height: 12; radius: 6; color: index % 3 ? "#00db80" : "#087dff"; anchors.verticalCenter: parent.verticalCenter }
                        Text { width: 118; height: 29; text: time; color: "#cfe0ef"; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 12 }
                        Text { width: 130; height: 29; text: type; color: "#cfe0ef"; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 12 }
                        Text { width: 130; height: 29; text: module; color: "#cfe0ef"; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 12 }
                        Text { width: 380; height: 29; text: content; color: "#cfe0ef"; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
                    }
                }
            }
        }
    }

    Component {
        id: photoView
        Item {
            Row { x: 9; y: 8; spacing: 10
                GButton {
                    width: 112; height: 34
                    text: photoLibrary.busy ? "同步中…" : "刷新列表"
                    onClicked: photoLibrary.refresh()
                }
                GButton { width: 150; height: 34; text: "取回最新一张"; onClicked: photoLibrary.fetchNewest() }
                GField { id: photoSearch; width: 208; height: 34; placeholder: "搜索图片名称" }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: "#8fa6ba"; font.pixelSize: 12
                    text: photoLibrary.status
                }
            }
            Flickable {
                objectName: "taskPhotoScroll"
                x: 9; y: 60; width: parent.width - 18
                height: Math.max(0, photoPagination.y - y - 10)
                contentWidth: width; contentHeight: photoGrid.implicitHeight
                clip: true; boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                Grid {
                    id: photoGrid
                    width: parent.width; columns: 6; spacing: 10
                    Repeater {
                        // 数据来自相机 TF 卡：photoLibrary 会自动把照片缓存到本机
                        model: photoLibrary.photos
                        delegate: Item {
                            property var entry: modelData
                            width: 123; height: 112
                            visible: entry.name.toLowerCase().indexOf(photoSearch.text.toLowerCase()) >= 0
                            Rectangle {
                                width: 123; height: 76; color: "#0a1c2b"; border.color: "#1d3b52"
                                Image {
                                    anchors.fill: parent; anchors.margins: 1
                                    asynchronous: true
                                    /* ⚠️ 这两行是性能关键，别删：
                                     * 相机照片是 2560×1440，而这里只显示 121×74。
                                     * 不设 sourceSize 就会按【全分辨率】解码成纹理
                                     * （每张 14.1 MB），16 张 = 225 MB；
                                     * 再叠加 cache:false 每次重绘都重新上传，
                                     * 直接把虚拟 GPU 管线打满 —— 表现是"切到图片列表
                                     * 之后视频剧烈抖动、拨杆延迟极高"（2026-10-15 实测）。
                                     * 按显示尺寸解码后每张约 0.14 MB，差 100 倍。 */
                                    sourceSize.width: 240
                                    sourceSize.height: 152
                                    cache: true
                                    fillMode: Image.PreserveAspectCrop
                                    source: entry.ready ? entry.localPath : ""
                                }
                                Text {
                                    anchors.centerIn: parent
                                    visible: !entry.ready
                                    color: "#5b7488"; font.pixelSize: 11
                                    text: "下载中…"
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: { page.inspectedPhoto = index; photoDialog.open() }
                            }
                            Text {
                                y: 80; width: 123; elide: Text.ElideMiddle
                                text: entry.name; color: "#dce9f7"; font.pixelSize: 12
                            }
                            Text {
                                y: 98
                                text: entry.ready ? (entry.sizeKB + " KB    已缓存") : "未缓存"
                                color: entry.ready ? "#00db80" : "#8fa6ba"; font.pixelSize: 11
                            }
                        }
                    }
                }
            }
            Row { id: photoPagination; anchors.bottom:parent.bottom; anchors.bottomMargin:10; anchors.right:parent.right; anchors.rightMargin:12; spacing:6
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color:"#aebfd0"; font.pixelSize:12
                    text: "共 " + photoLibrary.count + " 张"
                }
                GButton {
                    width: 88; height: 29; fill:"#061522"; text:"打开缓存目录"
                    onClicked: page.actionMessage("缓存目录：" + photoLibrary.cacheDirectory)
                }
            }
        }
    }

    Component {
        id: videoView
        Item {
            // ⚠️ 注意：外层 Component 的 id 是 videoView，它指的是【Component 本身】，
            //    组件内部不能用 videoView.xxx 访问本 Item 的属性（会得到 undefined）。
            //    所以这里统一用 page.videoSelection，不要再引入中间属性。
            id: videoRoot

            // ── 工具栏 ──
            Row { x: 9; y: 8; spacing: 10
                GButton {
                    width: 112; height: 34
                    text: videoLibrary.busy ? "同步中…" : "刷新列表"
                    onClicked: videoLibrary.refresh()
                }
                GButton {
                    width: 112; height: 34; text: "下载选中"
                    onClicked: {
                        var entry = page.selectedVideoEntry()
                        if (entry) {
                            videoLibrary.download(entry.name)
                        } else {
                            page.actionMessage("请先在左侧点选一个录像（列表里有 "
                                               + videoLibrary.photos.length + " 个，当前选中序号 "
                                               + page.selectedVideo + "）")
                        }
                    }
                }
                GButton {
                    width: 112; height: 34; text: "删除本地"
                    onClicked: {
                        var entry = page.selectedVideoEntry()
                        if (!entry) { page.actionMessage("请先在左侧点选一个录像"); return }
                        if (videoLibrary.removeLocal(entry.name))
                            page.actionMessage("已删除本地缓存（相机上的文件保留）")
                        else
                            page.actionMessage("该录像还没有本地缓存")
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: "#8fa6ba"; font.pixelSize: 12
                    text: videoLibrary.status
                }
            }

            // ── 下载进度（仅下载中显示）──
            Row {
                x: 9; y: 48; spacing: 10
                visible: videoLibrary.downloadingName.length > 0
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: "#dce9f7"; font.pixelSize: 12; width: 210
                    elide: Text.ElideMiddle
                    text: "下载中 " + videoLibrary.downloadingName
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 280; height: 10; radius: 5; color: "#0a1c2b"; border.color: "#1d3b52"
                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, videoLibrary.downloadProgress / 100))
                        height: parent.height; radius: 5; color: "#087dff"
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: "#9db1c5"; font.pixelSize: 12
                    text: videoLibrary.downloadingSizeText + "  "
                          + videoLibrary.downloadProgress.toFixed(0) + "%"
                }
            }

            // ── 左：录像列表（真实数据，双击即下载）──
            ListView {
                id: videoList
                x: 9; y: 72; width: 430
                height: Math.max(0, parent.height - 82)
                clip: true
                model: videoLibrary.photos
                spacing: 2
                delegate: Rectangle {
                    property var entry: modelData
                    width: videoList.width; height: 60
                    color: index === page.selectedVideo ? "#0a396d" : "#071927"
                    border.color: index === page.selectedVideo ? "#087dff" : "#15344b"
                    radius: 3
                    Text {
                        x: 12; y: 8; width: 390; elide: Text.ElideMiddle
                        text: entry.name; color: "#e7f1fb"; font.pixelSize: 13
                    }
                    Text {
                        x: 12; y: 31
                        text: entry.ready
                              ? ((entry.sizeKB >= 1024
                                  ? (entry.sizeKB / 1024).toFixed(1) + " MB"
                                  : entry.sizeKB + " KB") + "   已缓存到本机")
                              : "未下载（双击下载，或在右侧点「下载选中」）"
                        color: entry.ready ? "#00db80" : "#9db1c5"
                        font.pixelSize: 11
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: page.selectedVideo = index
                        onDoubleClicked: videoLibrary.download(entry.name)
                    }
                }
            }
            Text {
                x: 14; y: 82; width: 400; wrapMode: Text.WordWrap
                visible: videoLibrary.photos.length === 0
                color: "#5b7488"; font.pixelSize: 12
                text: videoLibrary.busy ? "正在读取相机录像列表…"
                                        : "相机 TF 卡里还没有录像。\n点右侧「录像」按钮开始录制，停止后会自动刷新到这里。"
            }

            // ── 右：选中录像的播放区 ──
            Rectangle {
                x: 449; y: 72; width: Math.max(0, parent.width - 458)
                height: Math.max(0, parent.height - 82)
                color: "#06131e"; border.color: "#1b415c"; radius: 4

                Text {
                    x: 10; y: 8; width: parent.width - 20; elide: Text.ElideMiddle
                    text: page.videoSelection ? page.videoSelection.name : "未选择录像"
                    color: "#dce9f7"; font.pixelSize: 12
                }

                // 已下载的 mp4 用本地播放器播放（file:// 路径）
                MediaPlayer {
                    id: localPlayer
                    autoPlay: false
                    source: (page.videoSelection && page.videoSelection.ready)
                            ? page.videoSelection.localPath : ""
                }
                VideoOutput {
                    id: localOutput
                    x: 8; y: 30
                    width: Math.max(0, parent.width - 16)
                    height: Math.max(0, parent.height - 152)
                    fillMode: VideoOutput.PreserveAspectFit
                    source: localPlayer
                }
                Text {
                    x: 12; y: 30
                    width: Math.max(0, parent.width - 24)
                    height: Math.max(0, parent.height - 152)
                    visible: !(page.videoSelection && page.videoSelection.ready)
                    color: "#5b7488"; font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    text: page.videoSelection
                          ? "该录像还在相机 TF 卡里\n点「下载选中」取回本机后即可在此播放"
                          : "在左侧选择一个录像\n（相机里有 " + videoLibrary.count + " 个）"

                }

                Row {
                    x: 10; anchors.bottom: parent.bottom; anchors.bottomMargin: 12; spacing: 8
                    GButton {
                        width: 76; height: 32; text: "播放"
                        onClicked: {
                            var entry = page.selectedVideoEntry()
                            if (entry && entry.ready) {
                                localPlayer.play()
                                page.actionMessage("正在播放 " + entry.name)
                            } else {
                                page.actionMessage("需要先下载到本机才能播放")
                            }
                        }
                    }
                    GButton { width: 76; height: 32; text: "暂停"; onClicked: localPlayer.pause() }
                    GButton { width: 76; height: 32; text: "停止"; onClicked: localPlayer.stop() }
                }
            }
        }
    }

    Dialog {
        id: routeDialog
        title:"导入航线 · JSON"
        width:640; height:500; x:(page.width-width)/2; y:(page.height-height)/2
        modal:true
        background: Rectangle {color:"#0a2234"; border.color:"#1b5279"; radius:6}
        contentItem: Column { spacing:12
            Text {text: page.indoorMode ? "局部航点数组：x / y / z / hover_sec（单位米、秒）" : "粘贴包含 lat / lon / alt / speed 字段的航点数组"; color:"#cfe0ef"; font.pixelSize:14}
            ScrollView { width:parent.width; height:340; TextArea {id:routeInput; selectByMouse:true; wrapMode:TextEdit.Wrap; color:"#dce9f7"; font.pixelSize:13; background:Rectangle {color:"#061522"}} }
            Row {spacing:10; GButton {text:"导入"; fill:"#087dff"; onClicked: page.indoorMode ? page.importLocal(routeInput.text) : page.importRoute()} GButton {text:"取消"; onClicked:routeDialog.close()} }
        }
    }
    Dialog {
        id: waypointDialog
        title:"编辑航点 "+(page.selectedWaypoint+1)
        width:420; height:280; x:(page.width-width)/2; y:(page.height-height)/2; modal:true
        background: Rectangle {color:"#0a2234"; border.color:"#1b5279"; radius:6}
        contentItem: Column {spacing:12
            Row { Text {text:"纬度"; width:70; color:"white"} GField {id:editLat; width:300} }
            Row { Text {text:"经度"; width:70; color:"white"} GField {id:editLon; width:300} }
            Row { Text {text:"高度 (m)"; width:70; color:"white"} GField {id:editAlt; width:300} }
            GButton {text:"保存"; fill:"#087dff"; onClicked: {
                var la=Number(editLat.text),lo=Number(editLon.text),a=Number(editAlt.text)
                if(!page.validCoordinate(la,lo)||!isFinite(a)||a<0||page.selectedWaypoint<0) {page.actionMessage("请输入有效经纬度和高度");return}
                page.moveWaypoint(page.selectedWaypoint,la,lo);waypoints.setProperty(page.selectedWaypoint,"alt",String(a));waypointDialog.close()
            } }
        }
    }
    Dialog {
        id: clearRouteDialog
        parent: Overlay.overlay
        title: "清空本地航线？"
        width: 420
        modal: true; standardButtons: Dialog.Ok | Dialog.Cancel
        x: (Overlay.overlay.width - width) / 2; y: (Overlay.overlay.height - height) / 2
        contentItem: Text { text: "将清空当前编辑航点；不会清除飞控任务。\n保存后才会覆盖已保存的本地航线。"; color: "#203040"; wrapMode: Text.WordWrap }
        onAccepted: {
            if (page.indoorMode) {
                page.clearLocalRoute()
            } else page.clearRoute()
        }
    }
    Popup {
        id:photoDialog; width:910; height:630; x:(page.width-width)/2; y:(page.height-height)/2; modal:true; focus:true
        background:Rectangle {color:"#061522"; border.color:"#087dff"; radius:5}
        contentItem:Item {
            // 显示相机里真实取回的照片（本地缓存路径）
            Image {
                id: previewImage
                anchors.fill: parent; anchors.bottomMargin: 44
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                source: {
                    var list = photoLibrary.photos
                    if (page.inspectedPhoto < 0 || page.inspectedPhoto >= list.length) return ""
                    var item = list[page.inspectedPhoto]
                    return item.ready ? item.localPath : ""
                }
            }
            Text {
                anchors.centerIn: parent
                visible: previewImage.status !== Image.Ready
                color: "#8fa6ba"; font.pixelSize: 14
                text: previewImage.source === "" ? "该照片尚未取回本地" : "正在载入…"
            }
            Text {
                anchors.left: parent.left; anchors.leftMargin: 10; anchors.bottom: parent.bottom; anchors.bottomMargin: 12
                color: "#cfe0ef"; font.pixelSize: 13
                text: {
                    var list = photoLibrary.photos
                    if (page.inspectedPhoto < 0 || page.inspectedPhoto >= list.length) return ""
                    var item = list[page.inspectedPhoto]
                    return item.name + (item.ready ? "    " + item.sizeKB + " KB" : "    （未缓存）")
                }
            }
            GButton {anchors.bottom:parent.bottom; anchors.bottomMargin:6; anchors.right:parent.right; anchors.rightMargin:10; text:"关闭预览"; onClicked:photoDialog.close()}
        }
    }

    Rectangle {
        id: toast
        z: 20
        width: toastText.width + 46
        height: 42
        radius: 6
        color: "#d90b2336"
        border.color: "#238ad7"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 180 } }
        Text { id: toastText; anchors.centerIn: parent; color: "white"; font.pixelSize: 14 }
        Timer { id: toastTimer; interval: 1800; onTriggered: toast.opacity = 0 }
    }
}
