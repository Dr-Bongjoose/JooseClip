// End-to-end pipeline test on real security footage — headless.
// Exercises: import -> proxy build -> timeline assembly -> fx -> strip
// generation. The export step runs once agent/exporter merges (the
// Exporter stub currently short-circuits with a message).
#include "medialibrary.h"
#include "decoder.h"
#include "proxymanager.h"
#include "thumbnailer.h"
#include "clipeffects.h"
#include "timelinewidget.h"
#include "exporter.h"

#include <QApplication>
#include <QCoreApplication>
#include <QImage>
#include <QThread>
#include <QDebug>
#include <cstdio>

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char *what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    ok ? ++g_pass : ++g_fail;
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    app.setApplicationName("JooseClip");

    const QString src = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                : QStringLiteral("/mnt/host/yup/clip (1).mp4");
    std::printf("E2E with source: %s\n", qPrintable(src));

    // 1. probe
    MediaInfo info = MediaLibrary::probe(src);
    check(info.hasVideo && info.width == 2880, "probe: 2880x1616 video found");
    check(info.hasAudio, "probe: audio track found");

    // 2. proxy build (synchronous here — the worker path is covered by the
    //    GUI; this validates the proxy itself)
    ProxyManager px;
    const QString proxy = px.proxyPath(src, info.width, info.height);
    if (!px.hasFreshProxy(src, info.width, info.height)) {
        std::printf("  building proxy (sync)…\n");
        bool ok = px.build(src, info.width, info.height);
        check(ok, "proxy build succeeded");
    }
    check(px.hasFreshProxy(src, info.width, info.height), "fresh proxy exists");
    const QString pb = px.playbackPath(src, info.width, info.height);
    check(pb != src, "playback path resolves to proxy");

    // 3. decode through the proxy: seek a frame mid-clip
    MediaInfo pinfo = MediaLibrary::probe(pb);
    check(pinfo.hasVideo && pinfo.width <= 960, "proxy probe: <=960 wide");
    Decoder dec(pinfo);
    double pts = 0;
    QImage img = dec.frameAt(5.0, pts);
    check(!img.isNull() && img.width() <= 960, "proxy decode at t=5s yields a frame");

    // 4. effects pipeline on the decoded frame
    ClipEffects fx;
    fx.brightness = 0.3;
    fx.saturation = 1.4;
    QImage before = img.copy();
    applyEffects(img, fx);
    bool changed = img != before;
    check(changed, "applyEffects visibly modifies the frame");

    // 5. filmstrip + waveform through the proxy
    Thumbnailer tn;
    QVector<QImage> strip = tn.filmstrip(pb, 0, 10.0, 8);
    check(strip.size() == 8, "filmstrip: 8 thumbs generated");
    bool allOk = true;
    for (const QImage &t : strip) if (t.isNull()) allOk = false;
    check(allOk, "filmstrip: no null thumbnails");
    QVector<double> peaks = tn.waveformPeaks(pb, 0, 10.0, 200);
    check(peaks.size() == 200, "waveform: 200 peaks");

    // 6. timeline assembly: two clips, trim + razor + undo (headless model ops)
    TimelineWidget tl;
    Clip c1; c1.path = src; c1.info = info; c1.duration = 10.0; c1.timelineStart = 0;
    Clip c2; c2.path = src; c2.info = info; c2.duration = 10.0; c2.timelineStart = 10.0;
    tl.addClipToTrack(0, c1, false);
    tl.addClipToTrack(0, c2, false);
    check(tl.model.v1.size() == 2, "timeline: two clips placed");
    tl.splitAt(5.0);
    check(tl.model.v1.size() == 3, "razor at 5s splits into 3 clips");
    check(tl.undo(), "undo restores 2-clip state");
    check(tl.model.v1.size() == 2, "undo verified");

    // 7. export through the (currently stubbed) exporter — verifies the
    //    dialog->exporter contract doesn't crash; real encode lands with
    //    the agent/exporter merge.
    TimelineModel model;
    model.v1 = tl.model.v1;
    Exporter ex;
    Exporter::Settings s;
    s.outPath = "/tmp/e2e_export_test.mp4";
    s.width = 1280; s.height = 720; s.fps = 30;
    bool ran = ex.run(model, s);
    std::printf("  (exporter stub ran=%d — real impl lands via agent/exporter)\n", int(ran));

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}