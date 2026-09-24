// ProxyManager standalone test — NOT part of the main app build (app.pro
// does not reference this file). Build it manually from any scratch dir:
//
//   qmake6 /path/to/repo/tests/test_proxy.pro && make && ./test_proxy
//
// Exercises the full proxy lifecycle against the small sample
// /tmp/opencode/jooseclip/sample.mp4 (12s, 640x360, h264+aac). It does NOT
// proxy the 2880x1616 security footage: transcoding those inside this VM is
// far too slow.

#include "proxymanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>
#include <cstdlib>
#include <string>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { printf("  PASS  %s\n", msg); ++g_pass; } \
        else      { printf("  FAIL  %s\n", msg); ++g_fail; } \
    } while (0)

static std::string shq(const QString &qs)
{
    const std::string s = qs.toStdString();
    std::string r = "'";
    for (char c : s) {
        if (c == '\'') r += "'\\''";
        else r += c;
    }
    r += "'";
    return r;
}

// Run cmd, capture stdout+stderr (mirrors ProxyManager's popen capture).
static std::string capture(const std::string &cmd)
{
    FILE *p = popen((cmd + " 2>&1").c_str(), "r");
    if (!p) return "(popen failed)";
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0)
        out.append(buf, n);
    pclose(p);
    return out;
}

// "W,H" of the first video stream, e.g. "640,360".
static bool ffprobeVideoDims(const QString &file, int *w, int *h)
{
    const std::string out = capture(
        "ffprobe -v error -select_streams v:0 "
        "-show_entries stream=width,height -of csv=p=0 " + shq(file));
    const size_t comma = out.find(',');
    if (comma == std::string::npos) return false;
    *w = std::atoi(out.substr(0, comma).c_str());
    *h = std::atoi(out.substr(comma + 1).c_str());
    return *w > 0 && *h > 0;
}

// "codec_name,sample_rate,channels" for the first audio stream, or "".
static std::string ffprobeAudio(const QString &file)
{
    return capture(
        "ffprobe -v error -select_streams a:0 -show_entries "
        "stream=codec_name,sample_rate,channels -of csv=p=0 " + shq(file));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("JooseClip")); // mirrors src/main.cpp

    const QString src = QStringLiteral("/tmp/opencode/jooseclip/sample.mp4");
    printf("ProxyManager test\nsource: %s\n", qPrintable(src));

    // Probe the source exactly like the app does (MediaInfo dims feed
    // proxyPath/build), but via ffprobe here to keep the test standalone.
    int srcW = 0, srcH = 0;
    if (!ffprobeVideoDims(src, &srcW, &srcH)) {
        printf("FATAL: ffprobe could not read source video dims\n");
        return 2;
    }
    printf("source dims (ffprobe): %dx%d\n\n", srcW, srcH);

    ProxyManager pm;
    const QString cache = pm.cacheDir();
    printf("cacheDir: %s\n", qPrintable(cache));
    CHECK(QFileInfo(cache).exists(), "ctor created the cache dir");
    CHECK(cache.endsWith(QLatin1String("/jooseclip/proxies")),
          "cache dir is <CacheLocation>/jooseclip/proxies");

    // Start from a clean cache so the assertions below are meaningful.
    printf("pre-clean freed %lld bytes\n", (long long)pm.clearAll());

    // --- proxyPath: determinism + naming --------------------------------
    const QString p1 = pm.proxyPath(src, srcW, srcH);
    const QString p2 = pm.proxyPath(src, srcW, srcH);
    CHECK(p1 == p2, "proxyPath deterministic (same call twice)");
    const QString name = QFileInfo(p1).fileName();
    const QString expectedSuffix = QStringLiteral("_%1x%2.mp4").arg(srcW).arg(srcH);
    CHECK(name.endsWith(expectedSuffix), "proxy name ends with <srcW>x<srcH>.mp4");
    const qsizetype hashLen = name.size() - expectedSuffix.size();
    bool hexOk = hashLen == 40;
    for (qsizetype i = 0; i < hashLen && hexOk; ++i) {
        const QChar c = name.at(i).toLower();
        hexOk = c.isDigit() || (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
    }
    CHECK(hexOk, "proxy name starts with 40-char hex sha1");
    CHECK(pm.proxyPath(src + QStringLiteral(".bak"), srcW, srcH) != p1,
          "different source path -> different proxy name (sha1 of path)");
    CHECK(QFileInfo(p1).absolutePath() == cache, "proxy lives in the cache dir");

    // --- pre-build state -------------------------------------------------
    CHECK(!pm.hasFreshProxy(src, srcW, srcH), "hasFreshProxy false before build");
    CHECK(pm.playbackPath(src, srcW, srcH) == src,
          "playbackPath falls back to source before build");

    // --- build ------------------------------------------------------------
    int built = 0, failed = 0;
    QString builtSrc, builtProxy, failSrc, failErr;
    QObject::connect(&pm, &ProxyManager::proxyBuilt,
                     [&](const QString &s, const QString &p) {
                         ++built; builtSrc = s; builtProxy = p; });
    QObject::connect(&pm, &ProxyManager::buildFailed,
                     [&](const QString &s, const QString &e) {
                         ++failed; failSrc = s; failErr = e; });

    CHECK(pm.build(src, srcW, srcH), "build() returns true");
    CHECK(built == 1 && failed == 0, "build emitted proxyBuilt (and no buildFailed)");
    CHECK(builtSrc == src && builtProxy == p1, "proxyBuilt carried source + proxy path");
    CHECK(QFile::exists(p1), "proxy file exists on disk");
    CHECK(pm.hasFreshProxy(src, srcW, srcH), "hasFreshProxy true after build");
    CHECK(pm.playbackPath(src, srcW, srcH) == p1,
          "playbackPath returns the proxy when fresh");

    // --- verify the transcode with ffprobe -------------------------------
    int pw = 0, ph = 0;
    const bool dimsOk = ffprobeVideoDims(p1, &pw, &ph);
    printf("proxy dims (ffprobe): %dx%d\n", pw, ph);
    CHECK(dimsOk, "proxy has a video stream (ffprobe)");
    CHECK(pw <= ProxyManager::kProxyLongEdge && ph <= ProxyManager::kProxyLongEdge,
          "proxy dims: long edge <= 960");
    CHECK(qAbs(double(pw) / ph - double(srcW) / srcH) < 0.02,
          "proxy preserves source aspect ratio");

    const std::string audio = ffprobeAudio(p1);
    printf("proxy audio (ffprobe): %s", audio.c_str());
    CHECK(!audio.empty(), "proxy has an audio stream");
    CHECK(audio.rfind("aac", 0) == 0, "proxy audio codec is aac");
    CHECK(audio.find("48000,2") != std::string::npos,
          "proxy audio is 48000 Hz stereo (per -ar 48000 -ac 2)");

    // --- freshness flips when the source is touched ------------------------
    CHECK(std::system(("touch " + shq(src)).c_str()) == 0, "touch source (bump mtime)");
    CHECK(!pm.hasFreshProxy(src, srcW, srcH),
          "hasFreshProxy false when source is newer than proxy");
    CHECK(pm.playbackPath(src, srcW, srcH) == src,
          "playbackPath falls back when proxy is stale");

    // --- clearAll ----------------------------------------------------------
    const qint64 proxySize = QFileInfo(p1).size();
    const qint64 freed = pm.clearAll();
    printf("clearAll freed %lld bytes (proxy was %lld)\n",
           (long long)freed, (long long)proxySize);
    CHECK(freed > 0, "clearAll returns > 0");
    CHECK(freed >= proxySize, "clearAll freed at least the proxy's size");
    CHECK(!QFile::exists(p1), "clearAll removed the proxy");
    CHECK(!pm.hasFreshProxy(src, srcW, srcH), "no fresh proxy after clearAll");
    CHECK(pm.playbackPath(src, srcW, srcH) == src,
          "playbackPath falls back after clearAll");

    // --- failure path: existing but non-media input ----------------------
    // Proves the popen stderr-tail capture: ffmpeg itself rejects the file.
    const QString garbage = QStringLiteral("/tmp/opencode/jooseclip/__garbage__.txt");
    {
        QFile f(garbage);
        f.open(QIODevice::WriteOnly);
        f.write("this is not a video file\n");
        f.close();
    }
    built = 0; failed = 0; failErr.clear();
    CHECK(!pm.build(garbage, srcW, srcH), "build() returns false for non-media input");
    CHECK(failed == 1 && built == 0,
          "non-media input emitted buildFailed (no proxyBuilt)");
    CHECK(failSrc == garbage, "buildFailed named the failing source");
    CHECK(failErr.contains(QLatin1String("Invalid data")),
          "buildFailed carries ffmpeg stderr tail");
    CHECK(!QFile::exists(pm.proxyPath(garbage, srcW, srcH)),
          "failed build left no partial proxy behind");
    QFile::remove(garbage);

    // --- failure path: missing source -------------------------------------
    const QString missing = QStringLiteral("/tmp/opencode/jooseclip/__no_such_file__.mp4");
    built = 0; failed = 0; failErr.clear();
    CHECK(!pm.build(missing, srcW, srcH), "build() returns false for missing source");
    CHECK(failed == 1 && built == 0,
          "missing source emitted buildFailed (no proxyBuilt)");
    CHECK(failErr.contains(QLatin1String("not found")),
          "buildFailed explains the missing source");

    // --- failure path: invalid dimensions ---------------------------------
    failed = 0;
    CHECK(!pm.build(src, 0, 0), "build() returns false for invalid dimensions");
    CHECK(failed == 1, "invalid dimensions emitted buildFailed");

    // --- clearAll with a missing cache dir --------------------------------
    CHECK(QDir(cache).removeRecursively(), "remove cache dir entirely");
    CHECK(pm.clearAll() == 0, "clearAll on missing dir returns 0");

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}