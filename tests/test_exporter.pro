QT += core gui widgets
CONFIG += c++17
TARGET = test_exporter
TEMPLATE = app

INCLUDEPATH += ../src
SOURCES += test_exporter.cpp \
    ../src/exporter.cpp \
    ../src/decoder.cpp \
    ../src/clipeffects.cpp \
    ../src/medialibrary.cpp \
    ../src/thumbnailer.cpp \
    ../src/theme.cpp
HEADERS += ../src/exporter.h \
    ../src/decoder.h \
    ../src/clipeffects.h \
    ../src/medialibrary.h \
    ../src/thumbnailer.h \
    ../src/theme.h

CONFIG += link_pkgconfig
PKGCONFIG += libavformat libavcodec libavutil libswscale libswresample