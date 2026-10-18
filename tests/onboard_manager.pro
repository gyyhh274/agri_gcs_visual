QT += core network testlib
CONFIG += c++17 testcase console
CONFIG -= app_bundle
macx: QMAKE_LIBS_OPENGL = -framework OpenGL
TARGET = onboard_manager_test
SOURCES += tst_onboard_manager.cpp ../src/OnboardManager.cpp
HEADERS += ../src/OnboardManager.h
