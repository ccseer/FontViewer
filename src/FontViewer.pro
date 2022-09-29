QT       += core gui widgets

CONFIG += c++11

SOURCES += \
    characterwidget.cpp \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    characterwidget.h \
    mainwindow.h

FORMS += \
    mainwindow.ui

INCLUDEPATH += ../
HEADERS += ../oitvar.h

VERSION = 1.0.0
QMAKE_TARGET_COMPANY = "1218.io"
QMAKE_TARGET_PRODUCT = "Seer"
QMAKE_TARGET_DESCRIPTION = "Seer - A Windows Quick Look Tool"
QMAKE_TARGET_COPYRIGHT = "Corey"
