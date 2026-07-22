QT += core network testlib
CONFIG += console c++20 testcase warn_on
CONFIG -= app_bundle
TEMPLATE = app
TARGET = lighttunnel_tests

INCLUDEPATH += ../src

SOURCES += \
    test_core.cpp \
    ../src/core/appsettings.cpp \
    ../src/core/coreupdatemanager.cpp \
    ../src/core/singboxconfigbuilder.cpp \
    ../src/core/xrayconfigbuilder.cpp \
    ../src/core/systemdcommandbuilder.cpp \
    ../src/core/vlessprofile.cpp

HEADERS += \
    ../src/core/appsettings.h \
    ../src/core/coreupdatemanager.h \
    ../src/core/singboxconfigbuilder.h \
    ../src/core/xrayconfigbuilder.h \
    ../src/core/systemdcommandbuilder.h \
    ../src/core/vlessprofile.h

QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic
