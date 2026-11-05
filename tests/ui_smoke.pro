QT += quick quickcontrols2 testlib positioning concurrent network
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TARGET = ui_smoke
SOURCES += tst_ui.cpp ../src/AsyncTileMapItem.cpp ../src/OfflineMapSource.cpp ../src/NestPosition.cpp ../src/AircraftProfile.cpp ../src/GroundLink.cpp
HEADERS += ../src/AsyncTileMapItem.h ../src/OfflineMapSource.h ../src/NestPosition.h ../src/AircraftProfile.h ../src/GroundLink.h
RESOURCES += ../qml.qrc

SOURCES += ../src/NestPlc.cpp ../src/ModbusTcpClient.cpp
HEADERS += ../src/NestPlc.h ../src/ModbusTcpClient.h

macx: QMAKE_LIBS_OPENGL = -framework OpenGL
SOURCES += ../src/NativeVideoItem.cpp ../src/GimbalLink.cpp ../src/PhotoLibrary.cpp ../src/RelayDiscovery.cpp
HEADERS += ../src/NativeVideoItem.h ../src/GimbalLink.h ../src/PhotoLibrary.h ../src/RelayDiscovery.h
