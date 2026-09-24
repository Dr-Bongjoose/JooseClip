#pragma once

#include <QString>
#include <QVector>
#include <QObject>
#include <atomic>

#include "decoder.h"
#include "timelinewidget.h"   // TimelineModel, Clip

// Renders the timeline to a finished video file with FFmpeg (encode side).
// Offline, frame-accurate: walks the timeline frame by frame, pulls video
// from per-source Decoders, mixes audio from the same decoders, encodes to
// H.264 + AAC in MP4.
class Exporter : public QObject {
    Q_OBJECT
public:
    struct Settings {
        QString outPath;
        int width = 1920;          // output resolution
        int height = 1080;
        int fps = 30;              // output frame rate
        int videoKbps = 8000;      // H.264 bitrate
        int audioKbps = 192;       // AAC bitrate
        bool useEffects = true;    // apply per-clip ClipEffects during export
    };

    explicit Exporter(QObject *parent = nullptr);

    // Estimate total output frames for progress reporting.
    static qint64 totalFrames(const TimelineModel &model, double fps);

    // Render [rangeStart, rangeEnd) of the timeline. Blocking; call from a
    // worker thread. Emits progress(n) and finished(ok, message) via the
    // event loop when invoked with Qt::QueuedConnection, or directly when
    // called synchronously (same-thread callers should use the signals).
    bool run(const TimelineModel &model, const Settings &settings,
             double rangeStart = 0.0, double rangeEnd = -1.0);

    void cancel() { cancelled_ = true; }

signals:
    void progress(qint64 framesDone);
    void finished(bool ok, const QString &message);

private:
    std::atomic<bool> cancelled_{false};
};