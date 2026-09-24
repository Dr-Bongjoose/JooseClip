# JooseClip

A lightweight cross-platform non-linear video editor (NLE) built with Qt 6 + FFmpeg.
Styled with the JooseBooks design system.

![status](https://img.shields.io/badge/status-early%20prototype-orange)

## Features (v0.2)

- Media bin: multi-select, drag-and-drop import, folder import (Ctrl+Shift+I), poster thumbnails
- Timeline: two video tracks (V1/V2), drag-and-drop, move, trim, snap, horizontal scroll with follow-playhead
- Razor at playhead (C), ripple delete (Shift+Del), multi-level undo/redo (Ctrl+Z / Ctrl+Shift+Z)
- Transport: Space play/pause, J/L shuttle, Left/Right frame step, Home/End, `\` fit-zoom, live mm:ss:ff timecode
- Per-clip color effects: brightness / contrast / saturation / gamma (Effect Controls panel, live preview, saved in project)
- Proxy media: high-res sources (e.g. 4K security footage) get 960p H.264 proxies built off-thread for smooth playback
- Timeline clips show filmstrip thumbnails + audio waveforms (generated asynchronously)
- Audio: FFmpeg-decoded PCM mixed through Qt Multimedia, audio-master clock sync
- Export: timeline render to H.264 + AAC MP4 (File → Export…, Ctrl+M)
- Project save/load (`.jcproj.json`, fx included), Window menu with resettable panel layout
- Theme: JooseBooks palette — #0D0D10 background, #17171D/#212129 surfaces, #D4A843 gold accent

## Building

Requirements: Qt 6 (widgets, multimedia), FFmpeg 4+ (libavformat, libavcodec, libavutil, libswscale, libswresample), qmake, C++17 compiler. `ffmpeg` on PATH for proxy generation.

```sh
qmake6 jooseclip.pro
make -j$(nproc)
./build/jooseclip
```

## Tests

```sh
# decoder smoke test (probe/seek/cache/audio)
cd build && make && ./tests/smoketest path/to/video.mp4

# module tests (each builds standalone)
for t in test_proxy test_thumbs test_props e2e_test; do
  mkdir -p /tmp/scratch/$t && cd /tmp/scratch/$t
  qmake6 <repo>/tests/$t.pro && make && ./$t
done
```

`e2e_test` exercises the full pipeline (import → proxy → decode → effects → strips → timeline) on real media; pass a source file as its argument.

## Layout

```
src/
  main.cpp            entry point + theme stylesheet
  mainwindow.{h,cpp}  main window, menus, transport, preview, proxy wiring
  timelinewidget.{h,cpp} timeline model + widget (undo, razor, selection, strips)
  decoder.{h,cpp}     per-file decoder: video frames + audio PCM (FFmpeg)
  audioengine.{h,cpp} pull-based mixer feeding QAudioSink
  medialibrary.{h,cpp} media bin + FFmpeg probe + poster thumbs
  clipeffects.{h,cpp} brightness/contrast/saturation/gamma (LUT pipeline)
  proxymanager.{h,cpp} 960p proxy builder/cache (ffmpeg CLI, worker threads)
  thumbnailer.{h,cpp} filmstrips + waveform peaks
  propertiespanel.{h,cpp} Effect Controls dock
  exporter.{h,cpp}   timeline → H.264/AAC MP4 renderer (libav)
  exportdialog.{h,cpp} export settings + progress dialog
  theme.{h,cpp}      JooseBooks palette + application stylesheet
tests/
  smoketest.cpp       decoder probe/seek/cache/audio suite
  test_proxy.cpp      proxy manager contract test
  test_thumbs.cpp     filmstrip/waveform test
  test_props.cpp      effect controls panel test
  e2e_test.cpp        full-pipeline test on real footage
```

## License

TBD