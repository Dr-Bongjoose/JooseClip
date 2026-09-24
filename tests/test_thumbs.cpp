// Standalone Thumbnailer contract test (NOT part of the main build).
// Usage: test_thumbs [media]
// Default media: /tmp/opencode/jooseclip/sample.mp4 (12s 640x360 H264+AAC).
//
// Verifies, per the Thumbnailer contract:
//   1. filmstrip(path, 0, 12, 10)        -> 10 non-null images, long edge <= 160
//   2. waveformPeaks(path, 0, 12, 40)    -> 40 values in [0,1], not all zero
//   3. poster(path, 64)                  -> non-null, both dimensions <= 64
// Plus contract edge cases (only run when the helper files exist):
//   - audio-only file: filmstrip emits nothing, poster null, peaks present
//   - video-only file: filmstrip works, peaks empty
#include "thumbnailer.h"
#include "decoder.h"
#include "medialibrary.h"

#include <QApplication>
#include <QFile>
#include <QImage>
#include <QVector>
#include <QDir>
#include <cstdio>

static int g_pass = 0, g_fail = 0;

static void check(bool ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++g_pass; else ++g_fail;
}

static int longEdge(const QImage &img)
{
    return qMax(img.width(), img.height());
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv); // Decoder/MediaLibrary pull in widgets types
    const QString path = argc > 1
        ? QString::fromLocal8Bit(argv[1])
        : QStringLiteral("/tmp/opencode/jooseclip/sample.mp4");

    if (!QFile::exists(path)) {
        printf("media not found: %s\n", qPrintable(path));
        return 2;
    }

    MediaInfo info = MediaLibrary::probe(path);
    printf("media: %s  %dx%d fps=%.2f dur=%.2fs video=%d audio=%d\n",
           qPrintable(path), info.width, info.height, info.fps,
           info.duration, info.hasVideo, info.hasAudio);

    Thumbnailer thumbs;

    // --- 1) filmstrip -------------------------------------------------------
    printf("filmstrip(path, 0, 12, 10):\n");
    QVector<QImage> strip = thumbs.filmstrip(path, 0.0, 12.0, 10);
    check(strip.size() == 10, "returns exactly 10 images");
    int nonNull = 0;
    for (const QImage &img : strip)
        if (!img.isNull()) ++nonNull;
    printf("  count=%d non-null=%d sizes:", int(strip.size()), nonNull);
    for (const QImage &img : strip)
        printf(" %dx%d", img.width(), img.height());
    printf("\n");
    check(nonNull == 10, "all 10 images non-null");
    bool allBounded = true;
    for (const QImage &img : strip)
        if (!img.isNull() && longEdge(img) > 160) allBounded = false;
    check(allBounded, "every image long edge <= 160");

    // sink variant must deliver the same 10 frames with indices 0..9 in order
    QVector<int> idxSeen;
    int sinkFrames = 0;
    thumbs.generateFilmstrip(path, 0.0, 12.0, 10,
                             [&](int index, const QImage &img) {
                                 idxSeen.append(index);
                                 if (!img.isNull() && longEdge(img) <= 160) ++sinkFrames;
                             });
    bool ordered = true;
    for (int i = 0; i < idxSeen.size(); ++i)
        if (idxSeen[i] != i) ordered = false;
    check(sinkFrames == 10 && ordered && idxSeen.size() == 10,
          "generateFilmstrip sink: 10 in-order indices 0..9, all <=160");

    // --- 2) waveform peaks -------------------------------------------------
    printf("waveformPeaks(path, 0, 12, 40):\n");
    QVector<double> peaks = thumbs.waveformPeaks(path, 0.0, 12.0, 40);
    check(peaks.size() == 40, "returns exactly 40 values");
    double pmin = 1.0, pmax = 0.0;
    for (double p : peaks) {
        if (p < 0.0 || p > 1.0) { pmin = -1.0; break; } // out of range
        pmin = qMin(pmin, p);
        pmax = qMax(pmax, p);
    }
    printf("  count=%d min=%.4f max=%.4f\n", int(peaks.size()), pmin, pmax);
    check(peaks.size() == 40 && pmin >= 0.0, "all values within [0,1]");
    check(pmax > 0.0, "not all zero (440Hz sine must register)");
    // text sparkline of the first 40 peaks for the record
    printf("  peaks:");
    for (double p : peaks) {
        int bars = int(p * 8 + 0.5);
        static const char *kBlocks[9] = {".", ":", "|", "i", "j", "J", "H", "#", "@"};
        printf("%s", kBlocks[qBound(0, bars, 8)]);
    }
    printf("\n");

    // --- 3) poster ----------------------------------------------------------
    printf("poster(path, 64):\n");
    QImage p64 = thumbs.poster(path, 64);
    printf("  %dx%d null=%d\n", p64.width(), p64.height(), p64.isNull());
    check(!p64.isNull(), "poster non-null");
    check(qMax(p64.width(), p64.height()) <= 64, "poster max dimension <= 64");
    check(qMax(p64.width(), p64.height()) == 64, "poster scaled up to long edge 64");

    // --- edge cases (need generated helper media next to the sample) --------
    const QString wavPath = QDir::temp().filePath(QStringLiteral("thumbs_test_audio_only.wav"));
    const QString vidPath = QDir::temp().filePath(QStringLiteral("thumbs_test_video_only.mp4"));
    if (QFile::exists(wavPath)) {
        printf("edge: audio-only %s\n", qPrintable(wavPath));
        QVector<QImage> s2 = thumbs.filmstrip(wavPath, 0.0, 1.0, 5);
        QImage po2 = thumbs.poster(wavPath, 64);
        check(s2.isEmpty() && po2.isNull(), "audio-only: filmstrip empty + poster null");
        QVector<double> pk2 = thumbs.waveformPeaks(wavPath, 0.0, 1.0, 8);
        check(pk2.size() == 8 && pk2.size() > 0 &&
              *std::max_element(pk2.begin(), pk2.end()) > 0.0,
              "audio-only: peaks still produced");
    } else {
        printf("edge: wav helper missing, skipped\n");
    }
    if (QFile::exists(vidPath)) {
        printf("edge: video-only %s\n", qPrintable(vidPath));
        QVector<double> pk3 = thumbs.waveformPeaks(vidPath, 0.0, 1.0, 8);
        QVector<QImage> s3 = thumbs.filmstrip(vidPath, 0.0, 1.0, 5);
        check(pk3.isEmpty(), "video-only: peaks empty");
        check(s3.size() == 5, "video-only: filmstrip still produced");
    } else {
        printf("edge: video-only helper missing, skipped\n");
    }

    printf("\nRESULT: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}