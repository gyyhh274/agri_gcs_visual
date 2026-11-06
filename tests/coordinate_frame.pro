QT += quick quickcontrols2 network testlib
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TARGET = coordinate_frame_test
SOURCES += tst_coordinate_frame.cpp ../src/GroundLink.cpp
HEADERS += ../src/GroundLink.h
RESOURCES += ../qml.qrc
