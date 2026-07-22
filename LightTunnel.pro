QT += core gui widgets network

CONFIG += c++20 warn_on
CONFIG -= debug_and_release
TEMPLATE = app
TARGET = lighttunnel

DEFINES += QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII

INCLUDEPATH += src

SOURCES += \
    src/main.cpp \
    src/core/appsettings.cpp \
    src/core/autostartmanager.cpp \
    src/core/coreupdatemanager.cpp \
    src/core/privilegedhelper.cpp \
    src/core/profilerepository.cpp \
    src/core/singboxconfigbuilder.cpp \
    src/core/xrayconfigbuilder.cpp \
    src/core/systemdcommandbuilder.cpp \
    src/core/systemdcoremanager.cpp \
    src/core/vlessprofile.cpp \
    src/ui/mainwindow.cpp \
    src/ui/opaquecombobox.cpp \
    src/ui/profilemanagerdialog.cpp \
    src/ui/settingsdialog.cpp \
    src/ui/statusindicator.cpp

HEADERS += \
    src/core/appsettings.h \
    src/core/autostartmanager.h \
    src/core/coreupdatemanager.h \
    src/core/privilegedhelper.h \
    src/core/profilerepository.h \
    src/core/singboxconfigbuilder.h \
    src/core/xrayconfigbuilder.h \
    src/core/systemdcommandbuilder.h \
    src/core/systemdcoremanager.h \
    src/core/vlessprofile.h \
    src/ui/mainwindow.h \
    src/ui/opaquecombobox.h \
    src/ui/profilemanagerdialog.h \
    src/ui/settingsdialog.h \
    src/ui/statusindicator.h

RESOURCES += resources/resources.qrc

QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic
QMAKE_LFLAGS += -Wl,-z,relro,-z,now

unix:!macx {
    target.path = /usr/bin
    desktop.path = /usr/share/applications
    desktop.files = packaging/io.github.lighttunnel.LightTunnel.desktop
    icons.path = /usr/share/icons/hicolor/scalable/apps
    icons.files = resources/icons/lighttunnel.svg
    policy.path = /usr/share/polkit-1/actions
    policy.files = packaging/io.github.lighttunnel.policy
    INSTALLS += target desktop icons policy
}
