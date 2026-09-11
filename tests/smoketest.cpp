// Headless smoke test: probe + frame seek through the app's own classes.
#include "medialibrary.h"
#include "decoder.h"
#include <QApplication>
#include <QImage>
#include <cstdio>
#include <vector>
#include <cstdint>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QString path = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("/tmp/opencode/jooseclip/sample.mp4");

    MediaInfo info = MediaLibrary::probe(path);
    printf("probe: %dx%d fps=%.2f dur=%.2fs video=%d audio=%d\n",
           info.width, info.height, info.fps, info.duration,
           info.hasVideo, info.hasAudio);

    Decoder dec(info);
    int ok = 0, fail = 0;
    for (double t : {0.0, 0.5, 1.7, 3.3, 4.9}) {
        double pts = 0;
        QImage img = dec.frameAt(t, pts);
        if (img.isNull()) { printf("seek %.1fs: FAIL\n", t); fail++; }
        else { printf("seek %.1fs: %dx%d pts=%.2f OK\n", t, img.width(), img.height(), pts); ok++; }
    }

    // sequential forward play (cache path, no reseek): expect pts 0, .04, .08 ...
    printf("sequential: ");
    int seqOk = 0;
    for (int i = 0; i < 10; ++i) {
        double t = i / 25.0 + 0.001;
        double pts = 0;
        QImage img = dec.frameAt(t, pts);
        double expect = i / 25.0;
        if (!img.isNull() && qAbs(pts - expect) < 0.05) seqOk++;
        else printf("\n  seq mismatch at i=%d pts=%.3f expect=%.3f null=%d", i, pts, expect, img.isNull());
    }
    printf(" %d/10 frames correct\n", seqOk);
    ok += seqOk; fail += 10 - seqOk;

    // long forward play: cache must stay bounded (RAM test)
    printf("forward 200 frames: ");
    int fwdOk = 0;
    for (int i = 0; i < 200; ++i) {
        double t = 0.5 + i / 25.0 + 0.001;
        double pts = 0;
        QImage img = dec.frameAt(t, pts);
        if (!img.isNull() && qAbs(pts - (0.5 + i / 25.0)) < 0.08) fwdOk++;
    }
    printf(" %d/200 frames correct\n", fwdOk);
    ok += fwdOk; fail += 200 - fwdOk;

    // backward jumps: exercise keyframe-seek path
    printf("backward seeks: ");
    int backOk = 0;
    for (double t : {2.0, 0.3, 4.5, 1.0, 5.5, 0.0}) {
        double pts = 0;
        QImage img = dec.frameAt(t, pts);
        if (!img.isNull() && pts <= t + 0.08) backOk++;
        else printf("\n  back seek %.1fs: pts=%.3f null=%d", t, pts, img.isNull());
    }
    printf(" %d/6 correct\n", backOk);
    ok += backOk; fail += 6 - backOk;

    // audio decode: ask twice in a row + after a video seek
    printf("audio decode: ");
    if (dec.hasAudio()) {
        std::vector<int16_t> pcm;
        int64_t got = dec.decodeAudio(0.0, 1.0, pcm);
        bool rateOk = dec.sampleRate() > 0;
        bool nonNull = got > 0;
        // second request should hit cache
        std::vector<int16_t> pcm2;
        int64_t got2 = dec.decodeAudio(0.0, 1.0, pcm2);
        bool cacheOk = got2 == got && !pcm2.empty();
        // random access after seek
        std::vector<int16_t> pcm3;
        int64_t got3 = dec.decodeAudio(3.0, 0.5, pcm3);
        bool seekOk = got3 > 0;
        printf("%s (rate=%d ch=%d got=%lld again=%lld seek=%lld)\n",
               (rateOk && nonNull && cacheOk && seekOk) ? "OK" : "FAIL",
               dec.sampleRate(), dec.channels(), (long long)got,
               (long long)got2, (long long)got3);
        ok += (rateOk && nonNull && cacheOk && seekOk) ? 1 : 0;
        fail += (rateOk && nonNull && cacheOk && seekOk) ? 0 : 1;
    } else {
        printf("skipped (no audio stream)\n");
    }

    printf("%d ok, %d fail\n", ok, fail);
    return fail == 0 ? 0 : 1;
}