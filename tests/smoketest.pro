QT += core gui widgets
CONFIG += c++17
TARGET = smoketest
TEMPLATE = app

INCLUDEPATH += ../src
SOURCES += smoketest.cpp ../src/decoder.cpp ../src/medialibrary.cpp
HEADERS += ../src/decoder.h ../src/medialibrary.h

CONFIG += link_pkgconfig
PKGCONFIG += libavformat libavcodec libavutil libswscale libswresample