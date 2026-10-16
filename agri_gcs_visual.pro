QT += quick quickcontrols2 positioning concurrent network
SOURCES += src/NativeVideoItem.cpp
HEADERS += src/NativeVideoItem.h
macx {
    # Recent macOS SDKs no longer contain the obsolete AGL framework.
    QMAKE_LIBS_OPENGL = -framework OpenGL
    GST_ROOT = $$(AGRI_GCS_GSTREAMER_ROOT)
    isEmpty(GST_ROOT): GST_ROOT = $$(HOME)/Documents/A_Workspace/MiniGUI_A8mini/.deps/GStreamer.framework/Versions/1.0
    !exists($$GST_ROOT/lib/pkgconfig/gstreamer-app-1.0.pc): GST_ROOT = /Library/Frameworks/GStreamer.framework/Versions/1.0
    exists($$GST_ROOT/lib/pkgconfig/gstreamer-app-1.0.pc) {
        DEFINES += AGRI_GCS_NATIVE_GSTREAMER
        DEFINES += AGRI_GCS_GSTREAMER_ROOT=\\\"$$GST_ROOT\\\"
        GST_QMLGL = $$clean_path($$GST_ROOT/../../../../build/gst-plugins)
        DEFINES += AGRI_GCS_QMLGL_PLUGIN_PATH=\\\"$$GST_QMLGL\\\"
        SOURCES += src/A8VideoPipeline.cpp
        HEADERS += src/A8VideoPipeline.h
        INCLUDEPATH += $$GST_ROOT/include/gstreamer-1.0 $$GST_ROOT/include/glib-2.0 $$GST_ROOT/lib/glib-2.0/include
        LIBS += -L$$GST_ROOT/lib -lgstapp-1.0 -lgstvideo-1.0 -lgstbase-1.0 -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0
        QMAKE_RPATHDIR += $$GST_ROOT $$GST_ROOT/lib
    }
}
CONFIG += c++17

SOURCES += src/main.cpp src/AsyncTileMapItem.cpp src/OfflineMapSource.cpp src/NestPosition.cpp src/AircraftProfile.cpp src/GroundLink.cpp src/GimbalLink.cpp src/PhotoLibrary.cpp src/RelayDiscovery.cpp
HEADERS += src/AsyncTileMapItem.h src/OfflineMapSource.h src/NestPosition.h src/AircraftProfile.h src/GroundLink.h src/GimbalLink.h src/PhotoLibrary.h src/RelayDiscovery.h
RESOURCES += qml.qrc

TARGET = agri_gcs_visual
VERSION = 1.4.0
QMAKE_TARGET_BUNDLE_PREFIX = local.agri.gcs
