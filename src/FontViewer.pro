QT       += core gui widgets
TARGET  = fontviewer
CONFIG += c++17

TEMPLATE = lib
CONFIG += plugin
TARGET_EXT = .dll
# SOURCES += test.cpp

SOURCES += \
    characterwidget.cpp \
    fontviewer.cpp \
    fontwidget.cpp

HEADERS += \
    characterwidget.h \
    fontviewer.h \
    fontwidget.h
    
FORMS += \
    fontwidget.ui

include(sdk.pri)

DISTFILES += ../bin/plugin.json

VERSION = 1.2.1
QMAKE_TARGET_COMPANY = "1218.io"
QMAKE_TARGET_PRODUCT = "Seer"
QMAKE_TARGET_DESCRIPTION = "Seer - A Windows Quick Look Tool"
QMAKE_TARGET_COPYRIGHT = "Corey"
