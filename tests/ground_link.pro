QT += core network testlib
CONFIG += c++17 testcase console
CONFIG -= app_bundle
TARGET = ground_link_test
SOURCES += tst_ground_link.cpp ../src/GroundLink.cpp
HEADERS += ../src/GroundLink.h
