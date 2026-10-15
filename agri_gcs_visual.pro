QT += quick quickcontrols2 positioning concurrent network
CONFIG += c++17

SOURCES += src/main.cpp src/AsyncTileMapItem.cpp src/OfflineMapSource.cpp src/NestPosition.cpp src/AircraftProfile.cpp src/GroundLink.cpp
HEADERS += src/AsyncTileMapItem.h src/OfflineMapSource.h src/NestPosition.h src/AircraftProfile.h src/GroundLink.h
SOURCES += src/GimbalLink.cpp src/PhotoLibrary.cpp src/RelayDiscovery.cpp src/MappingClient.cpp src/PointCloudView.cpp
SOURCES += src/PointCloudGpuItem.cpp
HEADERS += src/PointCloudGpuItem.h
HEADERS += src/GimbalLink.h src/PhotoLibrary.h src/RelayDiscovery.h src/MappingTypes.h src/MappingClient.h src/PointCloudView.h
RESOURCES += qml.qrc

TARGET = agri_gcs_visual
VERSION = 1.6.0
QMAKE_TARGET_BUNDLE_PREFIX = local.agri.gcs
