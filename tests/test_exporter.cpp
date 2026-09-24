// Standalone export test (NOT part of the app build).
//
// Builds a TimelineModel with two trimmed clips of sample.mp4:
//   clip A: sourceIn=1.0 duration=5.0 at timelineStart=0
//   clip B: sourceIn=0.0 duration=5.0 at timelineStart=5.0
// exports to /tmp/export_test.mp4 at 1280x720 @30fps via Exporter::run,
// then verifies the file with ffprobe (duration ~10s, video+audio present)
// and decodes a frame back out of the export with the app's own Decoder.
//
// Build: qmake6 tests/test_exporter.pro && make   (in a scratch build dir)
// Run:   ./test_exporter [sample.mp4]

#include "exporter.h"
#include "medialibrary.h"
#include "decoder.h"
#include "timelinewidget.h"

#include <QCoreApplication>
#include <QImage>
#include <QString>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int g_progressCalls = 0;
static qint64 g_lastProgress = -1;

static void onProgress(qint64 n) { g_progressCalls++; g_lastProgress = n; }

static bool g_finished = false;
static bool g_ok = false;
static QString g_msg;
static void onFinished(bool ok, const QString &msg)
{
    g_finished = true; g_ok = ok; g_msg = msg;
}

// Run a shell command, return its combined output.
static std::string runCmd(const char *cmd)
{
    std::string out;
    FILE *p = popen(cmd, "r");
    if (!p) return "popen failed";
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    pclose(p);
    return out;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);   // headless: no platform plugin needed

    const QString src = argc > 1
        ? QString::fromLocal8Bit(argv[1])
        : QStringLiteral("/tmp/opencode/jooseclip/sample.mp4");
    const QString out = QStringLiteral("/tmp/export_test.mp4");

    MediaInfo info = MediaLibrary::probe(src);
    printf("source probe: %dx%d fps=%.2f dur=%.2fs video=%d audio=%d\n",
           info.width, info.height, info.fps, info.duration,
           int(info.hasVideo), int(info.hasAudio));
    if (!info.hasVideo) {
        printf("FAIL: source has no video\n");
        return 1;
    }

    TimelineModel model;
    Clip a;
    a.path = src; a.info = info;
    a.sourceIn = 1.0; a.timelineStart = 0.0; a.duration = 5.0;
    Clip b = a;
    b.sourceIn = 0.0; b.timelineStart = 5.0; b.duration = 5.0;
    model.v1.push_back(a);
    model.v1.push_back(b);
    printf("timeline: A[sourceIn=1.0 start=0 dur=5] B[sourceIn=0 start=5 dur=5]  end=%.2fs\n",
           model.sequenceEnd());

    Exporter::Settings st;
    st.outPath = out;
    st.width = 1280;
    st.height = 720;
    st.fps = 30;
    st.videoKbps = 8000;
    st.audioKbps = 192;
    st.useEffects = true;

    printf("totalFrames(30fps) = %lld\n",
           (long long)Exporter::totalFrames(model, 30.0));

    Exporter exporter;
    QObject::connect(&exporter, &Exporter::progress, &onProgress);
    QObject::connect(&exporter, &Exporter::finished, &onFinished);

    const bool runOk = exporter.run(model, st);

    printf("run() returned %d\n", int(runOk));
    printf("progress: %d signals, last=%lld (expect 300)\n",
           g_progressCalls, (long long)g_lastProgress);
    printf("finished(%d, \"%s\")\n", int(g_ok), g_msg.toUtf8().constData());
    if (!runOk || !g_finished || !g_ok) {
        printf("FAIL: export did not complete successfully\n");
        return 1;
    }
    if (g_lastProgress != 300) {
        printf("FAIL: expected 300 progress frames, got %lld\n",
               (long long)g_lastProgress);
        return 1;
    }

    // ---- ffprobe verification ------------------------------------------
    const std::string cmd = "ffprobe -v error -show_entries "
        "format=duration:stream=index,codec_type,codec_name,width,height,"
        "avg_frame_rate,sample_rate,channels -of default=noprint_wrappers=1 "
        + std::string("\"") + out.toStdString() + "\"";
    const std::string probe = runCmd(cmd.c_str());
    printf("---- ffprobe %s ----\n%s", out.toUtf8().constData(), probe.c_str());
    printf("----------------------------------------\n");

    bool hasVideo = false, hasAudio = false;
    double duration = -1;
    int w = 0, h = 0;
    for (size_t pos = 0; pos < probe.size();) {
        size_t eol = probe.find('\n', pos);
        if (eol == std::string::npos) eol = probe.size();
        std::string line = probe.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.rfind("duration=", 0) == 0) duration = atof(line.c_str() + 9);
        else if (line.rfind("codec_type=video", 0) == 0) hasVideo = true;
        else if (line.rfind("codec_type=audio", 0) == 0) hasAudio = true;
        else if (line.rfind("width=", 0) == 0 && w == 0) w = atoi(line.c_str() + 6);
        else if (line.rfind("height=", 0) == 0 && h == 0) h = atoi(line.c_str() + 7);
    }

    int failures = 0;
    auto check = [&](bool cond, const char *what) {
        printf("%-34s %s\n", what, cond ? "OK" : "FAIL");
        if (!cond) failures++;
    };
    check(duration > 9.9 && duration < 10.1, "duration ~10s");
    check(hasVideo, "video stream present");
    check(hasAudio, "audio stream present");
    check(w == 1280 && h == 720, "resolution 1280x720");

    // ---- decode a frame back out of the export (playability proof) -----
    MediaInfo outInfo = MediaLibrary::probe(out);
    printf("export probe: %dx%d fps=%.2f dur=%.2fs video=%d audio=%d\n",
           outInfo.width, outInfo.height, outInfo.fps, outInfo.duration,
           int(outInfo.hasVideo), int(outInfo.hasAudio));
    Decoder dec(outInfo);
    double pts = 0.0;
    QImage img = dec.frameAt(5.5, pts);   // just past the A->B cut
    check(!img.isNull() && img.width() == 1280 && img.height() == 720,
          "re-decode frame at t=5.5s");
    QImage img2 = dec.frameAt(9.9, pts);
    check(!img2.isNull(), "re-decode frame at t=9.9s");

    printf("==== %s (failures=%d) ====\n", failures ? "TEST FAILED" : "TEST PASSED",
           failures);
    return failures ? 1 : 0;
}