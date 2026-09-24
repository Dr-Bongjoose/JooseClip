QT += core gui widgets
CONFIG += c++17
TARGET = e2e_test
TEMPLATE = app

INCLUDEPATH += ../src
SOURCES += e2e_test.cpp \
    ../src/decoder.cpp \
    ../src/medialibrary.cpp \
    ../src/timelinewidget.cpp \
    ../src/thumbnailer.cpp \
    ../src/proxymanager.cpp \
    ../src/clipeffects.cpp \
    ../src/theme.cpp \
    ../src/exporter.cpp \
    ../src/propertiespanel.cpp

HEADERS += ../src/decoder.h ../src/medialibrary.h ../src/timelinewidget.h \
    ../src/thumbnailer.h ../src/proxymanager.h ../src/clipeffects.h \
    ../src/theme.h ../src/exporter.h ../src/propertiespanel.h

CONFIG += link_pkgconfig
PKGCONFIG += libavformat libavcodec libavutil libswscale libswresample