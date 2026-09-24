# Standalone ProxyManager test — deliberately NOT included in app.pro or
# jooseclip.pro. Build it manually from any scratch dir:
#   mkdir build && cd build && qmake6 ../tests/test_proxy.pro && make
# $$PWD-based paths make it work from any build directory.

QT += core
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = test_proxy
TEMPLATE = app

INCLUDEPATH += $$PWD/../src
SOURCES += $$PWD/test_proxy.cpp $$PWD/../src/proxymanager.cpp
HEADERS += $$PWD/../src/proxymanager.h

# proxymanager.h includes decoder.h -> FFmpeg headers
CONFIG += link_pkgconfig
PKGCONFIG += libavformat libavcodec libavutil libswscale libswresample