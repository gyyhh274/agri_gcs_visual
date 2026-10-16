import QtQuick 2.15
import org.freedesktop.gstreamer.GLVideoItem 1.0

// qmlglsink 在 NativeVideoItem 创建时注册该类型，只有原生硬解后端会加载本文件。
GstGLVideoItem {
    objectName: "videoSurface"
}
