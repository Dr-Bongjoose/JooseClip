#pragma once

#include <QImage>
#include <QString>
#include <QObject>
#include <functional>

// Filmstrip thumbnails + audio waveforms for timeline clips.
// Generates visual data for a (path, sourceIn, duration) range so the
// timeline can draw pretty clips instead of flat rectangles.
class Thumbnailer : public QObject {
    Q_OBJECT
public:
    // Callback receives thumbnails in order, possibly from a worker thread.
    // Consumer must copy the QImage (cheap, COW).
    using ThumbSink = std::function<void(int index, const QImage &)>;

    explicit Thumbnailer(QObject *parent = nullptr);

    // Generate count evenly spaced thumbnails covering
    // [sourceIn, sourceIn+duration). Synchronous; decode cost is bounded by
    // count (typically 10-30 for a clip strip).
    void generateFilmstrip(const QString &path, double sourceIn, double duration,
                           int count, ThumbSink sink);

    // Convenience: whole-clip strip in one vector (for tests/properties).
    QVector<QImage> filmstrip(const QString &path, double sourceIn,
                              double duration, int count);

    // Waveform peaks for audio: count max-abs sample values covering
    // [sourceIn, sourceIn+duration), normalized 0..1. Empty on no audio.
    QVector<double> waveformPeaks(const QString &path, double sourceIn,
                                 double duration, int count);

    // Small square icon for the media bin (single frame, scaled).
    QImage poster(const QString &path, int size);
};