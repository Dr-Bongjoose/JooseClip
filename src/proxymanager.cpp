// ProxyManager implementation — cached ~960px proxies for smooth playback.
//
// See proxymanager.h for the public contract. Implementation notes:
//
// Proxy layout
//   <QStandardPaths::CacheLocation>/jooseclip/proxies/
//       <sha1-hex(absolute source path)>_<srcW>x<srcH>.mp4
// The source's dimensions are part of the NAME, so the same source probed at
// a different resolution never hits a mis-sized cache entry.
//
// Threading
//   - proxyPath()/hasFreshProxy()/playbackPath() are const, pure filesystem
//     checks (QFileInfo + QCryptographicHash) — safe from any thread.
//   - build() is SYNCHRONOUS AND BLOCKING by contract (it shells out to
//     ffmpeg and waits for the encode). It must be called from a worker
//     thread, not the GUI thread. It deliberately uses popen()/pclose()
//     instead of QProcess: QProcess needs a running QThread event loop to
//     deliver its finished() signal, which a bare worker thread does not
//     have. popen() is plain POSIX and works anywhere. (The header comment
//     mentions QProcess — the implementation intentionally deviates for
//     exactly this reason; the public API is unchanged.)
//   - clearAll() is a blocking directory sweep; it removes only *.mp4 files
//     and returns the number of bytes freed (0 for a missing/empty dir).
//
// Freshness
//   A proxy is fresh when proxy mtime >= source mtime. A missing source is
//   never considered fresh (its proxy is useless for playback of it).

#include "proxymanager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <sys/wait.h>
#include <cstdio>

namespace {

// Cap on retained ffmpeg stderr for the buildFailed payload.
constexpr int kTailCap = 8 * 1024;

// Single-quote a token for /bin/sh (embedded quotes become '\''').
QString shellQuote(const QString &s)
{
    QString escaped = s;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

// Proxy pixel size: long edge clamped to ProxyManager::kProxyLongEdge,
// aspect preserved, rounded DOWN to even (yuv420p requires even dims).
// Sources already at/below the cap are NOT upscaled — re-encoding at native
// size still gives the crf-28 proxy benefits without inventing pixels.
void proxyDims(int srcW, int srcH, int *outW, int *outH)
{
    const long long longEdge = qMax<long long>(srcW, srcH);
    const double f = longEdge > ProxyManager::kProxyLongEdge
            ? double(ProxyManager::kProxyLongEdge) / double(longEdge)
            : 1.0;
    const int w = int(double(srcW) * f + 0.5) & ~1;
    const int h = int(double(srcH) * f + 0.5) & ~1;
    *outW = qMax(2, w);
    *outH = qMax(2, h);
}

// Compact the captured ffmpeg stderr (progress lines use \r) into one
// readable line, keeping only the tail.
QString tailToMessage(QByteArray tail, int maxBytes)
{
    if (tail.size() > maxBytes)
        tail = tail.right(maxBytes);
    QString s = QString::fromLocal8Bit(tail);
    s.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    s.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return s.trimmed();
}

} // namespace

ProxyManager::ProxyManager(QObject *parent)
    : QObject(parent)
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.cache"); // last-resort fallback
    dir_ = base + QStringLiteral("/jooseclip/proxies");
    QDir().mkpath(dir_); // idempotent; build() retries if this ever fails
}

QString ProxyManager::cacheDir() const
{
    return dir_;
}

QString ProxyManager::proxyPath(const QString &sourcePath, int srcW, int srcH) const
{
    const QString abs = QFileInfo(sourcePath).absoluteFilePath();
    const QByteArray hash = QCryptographicHash::hash(
                abs.toUtf8(), QCryptographicHash::Sha1).toHex();
    return dir_ + QLatin1Char('/')
            + QString::fromLatin1(hash)
            + QStringLiteral("_%1x%2.mp4").arg(srcW).arg(srcH);
}

bool ProxyManager::hasFreshProxy(const QString &sourcePath, int srcW, int srcH) const
{
    const QFileInfo src(sourcePath);
    if (!src.exists())
        return false; // missing source: nothing to play back, proxy not fresh
    const QFileInfo px(proxyPath(sourcePath, srcW, srcH));
    return px.exists() && px.lastModified() >= src.lastModified();
}

QString ProxyManager::playbackPath(const QString &sourcePath, int srcW, int srcH) const
{
    return hasFreshProxy(sourcePath, srcW, srcH)
            ? proxyPath(sourcePath, srcW, srcH)
            : sourcePath;
}

bool ProxyManager::build(const QString &sourcePath, int srcW, int srcH)
{
    // Synchronous + blocking: shells out to ffmpeg and waits for the encode
    // to finish. Intended to be called from a worker thread; safe there
    // (popen/pclose, no QProcess/event-loop dependency). Signals are
    // emitted directly and firing them from any thread is safe.
    if (sourcePath.isEmpty() || srcW <= 0 || srcH <= 0) {
        emit buildFailed(sourcePath, QStringLiteral(
                    "proxy build: bad arguments (empty path or non-positive dimensions)"));
        return false;
    }
    const QFileInfo srcInfo(sourcePath);
    if (!srcInfo.exists() || !srcInfo.isFile()) {
        emit buildFailed(sourcePath, QStringLiteral("proxy build: source not found: %1")
                         .arg(sourcePath));
        return false;
    }
    if (!QDir().mkpath(dir_)) {
        emit buildFailed(sourcePath, QStringLiteral(
                    "proxy build: cannot create cache dir %1").arg(dir_));
        return false;
    }

    const QString out = proxyPath(sourcePath, srcW, srcH);
    int pw = 0, ph = 0;
    proxyDims(srcW, srcH, &pw, &ph);

    // Exact transcode contract:
    //   ffmpeg -y -nostdin -i SRC -vf scale=W:H -c:v libx264 -preset veryfast
    //          -crf 28 -c:a aac -ar 48000 -ac 2 OUT
    // The trailing 2>&1 folds ffmpeg's stderr (progress + errors) into the
    // pipe so failures can be reported.
    const QString cmd = QStringLiteral(
                "ffmpeg -y -nostdin -i %1 -vf scale=%2:%3"
                " -c:v libx264 -preset veryfast -crf 28"
                " -c:a aac -ar 48000 -ac 2 %4 2>&1")
            .arg(shellQuote(srcInfo.absoluteFilePath()))
            .arg(pw).arg(ph)
            .arg(shellQuote(out));

    FILE *p = popen(cmd.toLocal8Bit().constData(), "r");
    if (!p) {
        emit buildFailed(sourcePath, QStringLiteral(
                    "proxy build: popen failed for ffmpeg"));
        return false;
    }

    // Drain the pipe, keeping only the last kTailCap bytes.
    QByteArray tail;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0) {
        tail.append(buf, int(n));
        if (tail.size() > kTailCap)
            tail.remove(0, tail.size() - kTailCap);
    }
    const int status = pclose(p);

    bool exited = false;
    int exitCode = -1;
    if (status != -1) {
        if (WIFEXITED(status)) {
            exited = true;
            exitCode = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            exitCode = -WTERMSIG(status);
        }
    }

    const QFileInfo outInfo(out);
    const bool ok = exited && exitCode == 0
            && outInfo.exists() && outInfo.size() > 0;

    if (ok) {
        emit proxyBuilt(sourcePath, out);
        return true;
    }

    // Leave no half-written proxy behind: a partial file would look "fresh"
    // later and hand the decoder a broken stream.
    if (outInfo.exists())
        QFile::remove(out);

    QString err = tailToMessage(tail, 1500);
    if (err.isEmpty())
        err = QStringLiteral("(no ffmpeg output captured)");
    emit buildFailed(sourcePath, QStringLiteral("ffmpeg exit %1: %2")
                      .arg(exitCode).arg(err));
    return false;
}

qint64 ProxyManager::clearAll()
{
    const QDir d(dir_);
    if (!d.exists())
        return 0; // nothing cached yet
    const QFileInfoList files = d.entryInfoList(
                QStringList() << QStringLiteral("*.mp4"),
                QDir::Files | QDir::NoDotAndDotDot);
    qint64 freed = 0;
    for (const QFileInfo &fi : files) {
        const qint64 size = fi.size();
        if (QFile::remove(fi.absoluteFilePath()))
            freed += size; // only count what was actually removed
    }
    return freed;
}