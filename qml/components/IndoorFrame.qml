pragma Singleton
import QtQuick 2.15
import Qt.labs.settings 1.1

QtObject {
    id: frame
    // Routes and telemetry stay in world. Only the operator's map axes rotate.
    property QtObject preferences: Settings {
        category: "IndoorMapAxesV1"
        // Site direction confirmed by the operator: forward = world +X,
        // right = world -Y. Therefore map X = -world Y, map Y = world X.
        property real rotationDegrees: 90
        property bool forwardAxes: true
    }
    property string sessionId: ""
    property real rotationDeg: preferences.rotationDegrees
    readonly property bool forwardAxes: preferences.forwardAxes
    property bool calibrated: false
    property bool calibrationPending: false
    property real anchorX: 0
    property real anchorY: 0
    property string error: ""
    readonly property real minimumBaseline: 0.5
    signal calibrationChanged()

    function bindSession(session) {
        // A brief disconnect clears telemetry, but does not create a new map.
        if (!session || session === sessionId) return
        sessionId = session
        rotationDeg = preferences.rotationDegrees
        calibrated = false
        calibrationPending = false
        error = ""
        calibrationChanged()
    }
    function toDisplay(x, y) {
        var radians = rotationDeg * Math.PI / 180
        return Qt.point(Math.cos(radians) * x - Math.sin(radians) * y,
                        Math.sin(radians) * x + Math.cos(radians) * y)
    }
    function toWorld(x, y) {
        var radians = rotationDeg * Math.PI / 180
        return Qt.point(Math.cos(radians) * x + Math.sin(radians) * y,
                        -Math.sin(radians) * x + Math.cos(radians) * y)
    }
    function validSession(session) {
        if (session && session === sessionId) return true
        error = "定位会话已变化，请重新记录起点。"
        return false
    }
    function beginCalibration(x, y, session) {
        if (!validSession(session) || !isFinite(x) || !isFinite(y)) return false
        anchorX = x
        anchorY = y
        calibrationPending = true
        error = ""
        return true
    }
    function finishCalibration(x, y, session) {
        if (!validSession(session) || !calibrationPending || !isFinite(x) || !isFinite(y)) return false
        var dx = x - anchorX, dy = y - anchorY
        if (Math.sqrt(dx * dx + dy * dy) < minimumBaseline) {
            error = "位移不足 0.5 m，请沿实际前方直走更远后重试。"
            return false
        }
        return setRotation(90 - Math.atan2(dy, dx) * 180 / Math.PI, session)
    }
    function setRotation(degrees, session) {
        if (!validSession(session) || !isFinite(degrees)) return false
        rotationDeg = ((degrees + 180) % 360 + 360) % 360 - 180
        preferences.rotationDegrees = rotationDeg
        preferences.forwardAxes = true
        preferences.sync()
        calibrated = true
        calibrationPending = false
        error = ""
        calibrationChanged()
        return true
    }
    function resetRotation(session) {
        if (!validSession(session)) return false
        rotationDeg = 0
        preferences.rotationDegrees = 0
        preferences.forwardAxes = false
        preferences.sync()
        calibrated = false
        calibrationPending = false
        error = ""
        calibrationChanged()
        return true
    }
}
