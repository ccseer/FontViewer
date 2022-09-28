QT       += core gui  widgets

CONFIG += c++11

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    ../../OitViewer/OitViewer/oitvar.h \
    mainwindow.h

FORMS += \
    mainwindow.ui

INCLUDEPATH += ../../OitViewer/OitViewer/

VERSION = 1.1.0
include(../../com.pri)
