QT += core gui widgets multimedia
CONFIG += c++17
TEMPLATE = app
TARGET = test_audio
INCLUDEPATH += ../src

SOURCES += test_audio.cpp \
    ../src/audioengine.cpp \
    ../src/decoder.cpp \
    ../src/medialibrary.cpp \
    ../src/timelinewidget.cpp \
    ../src/thumbnailer.cpp \
    ../src/theme.cpp

HEADERS += ../src/audioengine.h \
    ../src/decoder.h \
    ../src/medialibrary.h \
    ../src/timelinewidget.h \
    ../src/thumbnailer.h \
    ../src/clipeffects.h \
    ../src/theme.h

CONFIG += link_pkgconfig
PKGCONFIG += libavcodec libavformat libavutil libswscale libswresample