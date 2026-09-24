#include "thumbnailer.h"
#include "decoder.h"
#include "medialibrary.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// Cost model (every call is synchronous on the caller's thread — the GUI
// thread for now, so callers should keep count/duration bounded):
//
//   generateFilmstrip : one probe + one Decoder open + count frame requests.
//       Consecutive slot centers usually sit inside the Decoder's kSeekGap
//       forward-decode window, so a strip costs roughly one decode pass over
//       [sourceIn, sourceIn+duration); wide centers pay a keyframe seek plus
//       one GOP decode per jump. Bounded by count * GOP.
//   waveformPeaks     : one probe + one audio Decoder open +
//       ceil(duration/0.25) decodeAudio calls (~0.5s decode window each),
//       i.e. about 2x the clip's audio duration in decode work, independent
//       of the requested peak count.
//   poster            : one probe + one frame decode + one smooth scale.
//
// Thread safety: Thumbnailer is stateless — each call constructs its own
// private Decoder, so calls never share FFmpeg state with each other or
// with the decoder registry MainWindow owns.
// ---------------------------------------------------------------------------

namespace {

constexpr int   kThumbLongEdge  = 160;  // filmstrip thumbnail long edge, px
constexpr double kAudioChunkSec = 0.25; // waveform analysis chunk, seconds

// Scale so the longer edge is exactly `edge`, aspect preserved.
// QImage::scaled with KeepAspectRatio + a square bound does exactly that
// (and upscales smaller sources, which is what the strip wants).
QImage scaledToLongEdge(const QImage &img, int edge)
{
    if (img.isNull() || edge <= 0)
        return QImage();
    return img.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace

Thumbnailer::Thumbnailer(QObject *parent) : QObject(parent) {}

// ---------------------------------------------------------------------------
// Filmstrip
// ---------------------------------------------------------------------------

void Thumbnailer::generateFilmstrip(const QString &path, double sourceIn,
                                    double duration, int count, ThumbSink sink)
{
    if (count <= 0 || duration <= 0.0 || !sink)
        return;

    const MediaInfo info = MediaLibrary::probe(path);
    if (!info.hasVideo)
        return; // no video stream: nothing to emit (audio-only clip)

    // Private per-call decoder — no shared state, so this is safe to call
    // from any thread or concurrently with other Thumbnailer calls.
    Decoder dec(info);

    for (int i = 0; i < count; ++i) {
        // Center of slot i across [sourceIn, sourceIn+duration):
        // sourceIn + (i + 0.5) * duration / count
        const double center = sourceIn + (i + 0.5) * duration / count;
        double pts = 0.0;
        const QImage frame = dec.frameAt(center, pts);
        if (frame.isNull())
            continue; // decode failed for this slot: skip it, keep order
        sink(i, scaledToLongEdge(frame, kThumbLongEdge));
    }
}

QVector<QImage> Thumbnailer::filmstrip(const QString &path, double sourceIn,
                                      double duration, int count)
{
    QVector<QImage> out;
    if (count > 0)
        out.reserve(count);
    generateFilmstrip(path, sourceIn, duration, count,
                      [&out](int, const QImage &img) { out.append(img); });
    return out;
}

// ---------------------------------------------------------------------------
// Waveform peaks
// ---------------------------------------------------------------------------

QVector<double> Thumbnailer::waveformPeaks(const QString &path, double sourceIn,
                                          double duration, int count)
{
    if (count <= 0)
        return {};

    const MediaInfo info = MediaLibrary::probe(path);
    if (!info.hasAudio)
        return {}; // no audio stream: empty vector (contract)

    Decoder dec(info); // private per-call decoder, audio side opens lazily

    // 1) Decode the range in kAudioChunkSec chunks, tracking max |s16| per
    //    chunk (interleaved channels all feed the same peak — right for a
    //    display waveform). decodeAudio appends to `pcm`, returns per-channel
    //    sample count or -1 on error.
    const int nChunks = duration > 0.0 ? int(std::ceil(duration / kAudioChunkSec)) : 0;
    QVector<int> chunkPeak(nChunks, 0); // max |s16| per chunk: 0..32768

    std::vector<int16_t> pcm;
    for (int c = 0; c < nChunks; ++c) {
        const double from = sourceIn + double(c) * kAudioChunkSec;
        const double secs = std::min(kAudioChunkSec,
                                     duration - double(c) * kAudioChunkSec);
        pcm.clear();
        if (dec.decodeAudio(from, secs, pcm) < 0)
            continue; // decode error: treat chunk as silence
        int peak = 0;
        for (int16_t s : pcm) {
            const int a = std::abs(int(s));
            if (a > peak) peak = a;
        }
        chunkPeak[c] = peak;
    }

    // 2) Max-merge chunk peaks into exactly `count` buckets. Bucket j covers
    //    chunk indices [floor(j*nChunks/count), ceil((j+1)*nChunks/count)):
    //    the half-open bounds keep every bucket non-empty when count >
    //    nChunks (a chunk may feed two neighbouring buckets) and each bucket
    //    spans several chunks when count < nChunks. Zero chunks -> zero peaks.
    QVector<double> peaks(count, 0.0);
    for (int j = 0; j < count && nChunks > 0; ++j) {
        const int lo = int(double(j) * nChunks / count);            // [0, nChunks-1]
        const int hi = int(std::ceil(double(j + 1) * nChunks / count)); // [1, nChunks]
        double m = 0.0;
        for (int c = qMax(0, lo); c < hi && c < nChunks; ++c)
            m = std::max(m, double(chunkPeak[c]));
        peaks[j] = m;
    }

    // 3) Normalize to 0..1 by the overall max; an all-zero range stays
    //    all-zero rather than dividing by zero.
    int overall = 0;
    for (int c = 0; c < nChunks; ++c)
        overall = std::max(overall, chunkPeak[c]);
    if (overall > 0) {
        for (double &p : peaks)
            p = qBound(0.0, p / double(overall), 1.0);
    }
    return peaks;
}

// ---------------------------------------------------------------------------
// Poster
// ---------------------------------------------------------------------------

QImage Thumbnailer::poster(const QString &path, int size)
{
    if (size <= 0)
        return QImage();

    const MediaInfo info = MediaLibrary::probe(path);
    if (!info.hasVideo)
        return QImage(); // no video stream: null poster

    Decoder dec(info);
    double pts = 0.0;
    const QImage frame = dec.frameAt(0.0, pts); // first video frame
    if (frame.isNull())
        return QImage();
    return frame.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}