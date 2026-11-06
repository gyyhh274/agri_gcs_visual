QT += core network testlib
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TARGET = joint_mission_test
SOURCES += tst_joint_mission.cpp ../src/GroundLink.cpp
HEADERS += ../src/GroundLink.h
