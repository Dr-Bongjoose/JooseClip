# Standalone PropertiesPanel test — NOT part of the main build.
# Build manually in a scratch dir, e.g.:
#   mkdir -p build-props && cd build-props
#   qmake6 ../tests/test_props.pro && make -j8
#   QT_QPA_PLATFORM=offscreen ./test_props
QT += core gui widgets
CONFIG += c++17
TARGET = test_props
TEMPLATE = app

INCLUDEPATH += ../src

SOURCES += \
    test_props.cpp \
    ../src/timelinewidget.cpp \
    ../src/propertiespanel.cpp \
    ../src/clipeffects.cpp \
    ../src/theme.cpp \
    ../src/medialibrary.cpp \
    ../src/thumbnailer.cpp \
    ../src/decoder.cpp

HEADERS += \
    ../src/timelinewidget.h \
    ../src/propertiespanel.h \
    ../src/medialibrary.h \
    ../src/decoder.h \
    ../src/clipeffects.h \
    ../src/theme.h \
    ../src/thumbnailer.h

# timelinewidget.cpp -> MediaLibrary::probe (medialibrary.cpp) needs FFmpeg
CONFIG += link_pkgconfig
PKGCONFIG += libavformat libavcodec libavutil libswscale libswresample