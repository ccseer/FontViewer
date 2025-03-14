QT       += core gui widgets

# TEMPLATE = lib
# CONFIG += plugin
SOURCES += test.cpp

CONFIG += c++17


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

DISTFILES += fontviewer.json

VERSION = 1.2.0
QMAKE_TARGET_COMPANY = "1218.io"
QMAKE_TARGET_PRODUCT = "Seer"
QMAKE_TARGET_DESCRIPTION = "Seer - A Windows Quick Look Tool"
QMAKE_TARGET_COPYRIGHT = "Corey"
