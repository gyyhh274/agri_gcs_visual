QT += quick quickcontrols2 testlib positioning concurrent network
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TARGET = ui_smoke
SOURCES += tst_ui.cpp ../src/AsyncTileMapItem.cpp ../src/OfflineMapSource.cpp ../src/NestPosition.cpp ../src/AircraftProfile.cpp ../src/GroundLink.cpp
HEADERS += ../src/AsyncTileMapItem.h ../src/OfflineMapSource.h ../src/NestPosition.h ../src/AircraftProfile.h ../src/GroundLink.h
RESOURCES += ../qml.qrc
