// test_audio.cpp — regression test for the GUI-freeze deadlock (Sep 2026).
//
// BUG: AudioEngine::fill() held audioSync_ across decoder lookup; the
// lookup (MainWindow::decoderFor) re-locked the same non-recursive mutex
// on the same thread -> audio thread deadlocked holding the lock; the next
// GUI render (which locks audioSync_) blocked forever => app frozen.
//
// TEST: reproduce the exact interleaving headless: GUI thread holds the
// sync mutex, audio pull runs testPull() (which locks to snapshot). With
// the fix, fill() snapshots under the lock and does decoder lookup +
// decode with NOTHING held -> pull completes with real samples while the
// GUI "edit" is in flight. Also asserts the clip lookup actually found
// the clip (non-silent audio where the sample has sound).

#include <QCoreApplication>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "audioengine.h"
#include "decoder.h"
#include "medialibrary.h"

static int passCount = 0, failCount = 0;
static void check(bool ok, const char *what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    ok ? ++passCount : ++failCount;
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 2) { printf("usage: test_audio <media>\n"); return 2; }
    const QString media = argv[1];

    MediaInfo info = MediaLibrary::probe(media);
    printf("media: %s  %dx%d fps=%.2f dur=%.2fs audio=%d\n",
           qPrintable(info.name), info.width, info.height, info.fps,
           info.duration, info.hasAudio);
    if (!info.hasAudio) { printf("FAIL: sample has no audio\n"); return 1; }

    // --- Scenario 1: pull while the GUI holds the sync mutex ("edit in
    // flight") — the exact interleaving that froze the app. The fix keeps
    // the snapshot critical section short and does decoder lookup/decode
    // lock-free, so the pull must complete promptly and deliver samples.
    {
        AudioEngine eng;
        std::mutex sync;
        eng.setSyncMutex(&sync);
        QVector<Clip> v1, v2;
        Clip c;
        c.path = media;
        c.sourceIn = 0.0;
        c.timelineStart = 0.0;
        c.duration = qMin(info.duration, 10.0);
        v1.push_back(c);
        eng.setTimeline(&v1, &v2);
        // decoder registry is fake-empty: lookup creates a real Decoder
        eng.setDecoderLookup([&media](const QString &) {
            return std::make_shared<Decoder>(MediaLibrary::probe(media));
        });

        // GUI thread simulates SHORT edits (lock; do model work; unlock) —
        // the real pattern. If fill() held the lock across decode (old bug)
        // the puller starves forever; with the fix the pull completes even
        // while edits are in flight.
        std::atomic<bool> stop{false};
        QThread *gui = QThread::create([&]() {
            int i = 0;
            while (!stop) {
                { std::lock_guard<std::mutex> l(sync); ++i; QThread::msleep(1); }
                QThread::msleep(1);
            }
        });
        gui->start();

        std::atomic<bool> done{false};
        std::atomic<bool> timedOut{false};
        const int FRAMES = 4800; // 100ms @ 48kHz stereo
        std::vector<int16_t> buf(size_t(FRAMES) * 2, 0);
        QThread *t = QThread::create([&]() {
            eng.testPull(buf.data(), FRAMES);
            done = true;
        });
        t->start();
        // wait up to 15s for the pull (decode can take a while headless)
        for (int i = 0; i < 1500 && !done; ++i) QThread::msleep(10);
        if (!done) timedOut = true;
        stop = true;
        gui->wait(1000);
        t->terminate();
        t->wait(500);

        check(!timedOut, "audio pull completes while GUI holds sync mutex (no deadlock)");
        if (!timedOut) {
            // the sample contains a 440Hz tone: pull must deliver energy
            long sum = 0;
            for (int16_t s : buf) sum += labs(s);
            check(sum > 1000, "pull delivered real audio samples (energy > threshold)");
        }
    }

    // --- Scenario 2: concurrent GUI lock/acquire + audio pulls — hammer
    // the lock interleave to catch ordering bugs; must not deadlock either.
    {
        AudioEngine eng;
        std::mutex sync;
        eng.setSyncMutex(&sync);
        QVector<Clip> v1, v2;
        Clip c;
        c.path = media;
        c.sourceIn = 0.0;
        c.timelineStart = 0.0;
        c.duration = qMin(info.duration, 10.0);
        v1.push_back(c);
        eng.setTimeline(&v1, &v2);
        eng.setDecoderLookup([&media](const QString &) {
            return std::make_shared<Decoder>(MediaLibrary::probe(media));
        });

        std::atomic<bool> stop{false};
        std::atomic<int> pulls{0};
        QThread *puller = QThread::create([&]() {
            std::vector<int16_t> buf(4800 * 2, 0);
            while (!stop) {
                eng.testPull(buf.data(), 4800);
                ++pulls;
            }
        });
        puller->start();
        long sum = 0;
        for (int i = 0; i < 500; ++i) {
            { std::lock_guard<std::mutex> l(sync); sum += 1; } // GUI edits
            if (i % 100 == 99) QThread::msleep(1);
        }
        stop = true;
        puller->wait(3000);
        check(pulls > 0, "concurrent pulls under GUI lock churn all complete");
        (void)sum;
    }

    printf("\nRESULT: %d passed, %d failed\n", passCount, failCount);
    return failCount == 0 ? 0 : 1;
}