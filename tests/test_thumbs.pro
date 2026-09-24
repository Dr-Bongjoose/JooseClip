QT += core gui widgets
CONFIG += c++17
TARGET = test_thumbs
TEMPLATE = app

INCLUDEPATH += ../src
SOURCES += test_thumbs.cpp ../src/thumbnailer.cpp ../src/decoder.cpp ../src/medialibrary.cpp
HEADERS += ../src/thumbnailer.h ../src/decoder.h ../src/medialibrary.h

CONFIG += link_pkgconfig
PKGCONFIG += libavformat libavcodec libavutil libswscale libswresample