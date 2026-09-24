QT += core gui widgets multimedia
CONFIG += c++17
TARGET = jooseclip
TEMPLATE = app

SOURCES += \
    src/main.cpp \
    src/decoder.cpp \
    src/medialibrary.cpp \
    src/timelinewidget.cpp \
    src/mainwindow.cpp \
    src/audioengine.cpp \
    src/clipeffects.cpp \
    src/theme.cpp \
    src/proxymanager.cpp \
    src/thumbnailer.cpp \
    src/propertiespanel.cpp

HEADERS += \
    src/decoder.h \
    src/medialibrary.h \
    src/timelinewidget.h \
    src/mainwindow.h \
    src/audioengine.h \
    src/clipeffects.h \
    src/theme.h \
    src/proxymanager.h \
    src/thumbnailer.h \
    src/propertiespanel.h

# FFmpeg (C API — the "C" part you'd have written by hand)
CONFIG += link_pkgconfig
PKGCONFIG += libavformat libavcodec libavutil libswscale libswresample