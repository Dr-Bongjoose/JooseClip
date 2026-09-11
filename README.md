# JooseClip

A lightweight cross-platform non-linear video editor (NLE) built with Qt 6 + FFmpeg.

![status](https://img.shields.io/badge/status-early%20prototype-orange)

## Features (v0.1)

- Media bin: drop files to import (probed via FFmpeg)
- Timeline: two video tracks (V1/V2), drag-and-drop, move, trim, snap
- Razor at playhead (C), ripple delete (Shift+Del)
- Play/pause preview (Space) with video monitor render
- Audio: FFmpeg-decoded PCM mixed through Qt Multimedia, V1+V2 summing
- Bounded frame cache (~1s at 24fps) with keyframe-seek for random access
- Multi-level undo/redo (100 steps, Ctrl+Z / Ctrl+Shift+Z)
- Project save/load (`.jcproj.json`)
- Window menu: all panels re-openable, reset layout

## Building

Requirements: Qt 6 (widgets, multimedia), FFmpeg 4+ (libavformat, libavcodec, libavutil, libswscale, libswresample), qmake, C++17 compiler.

```sh
qmake6 jooseclip.pro
make -j$(nproc)
./build/jooseclip
```

Run the headless decoder smoke test:

```sh
cd build && make
./build/tests/smoketest            # uses /tmp/opencode/jooseclip/sample.mp4
./build/tests/smoketest path.mp4   # or your own file
```

## Layout

```
src/
  main.cpp            entry point
  mainwindow.{h,cpp}  main window, menus, monitor, playback clock
  timelinewidget.{h,cpp} timeline model + widget (undo, razor, ripple, trim)
  decoder.{h,cpp}     per-file decoder: video frames + audio PCM (FFmpeg)
  audioengine.{h,cpp} pull-based mixer feeding QAudioSink
  medialibrary.{h,cpp} media bin + FFmpeg probe
tests/smoketest.cpp  headless probe/seek/cache/audio test
```

## License

TBD