#pragma once

#include <QString>
#include <QHash>
#include <QObject>

#include "decoder.h"

// Proxy media manager: security-cam footage (2880x1616 H.264 High) cannot
// decode in real time inside this VM, so playback uses low-res proxies.
// A proxy is a cached MP4 beside the source (not next to it — in a
// dedicated cache dir) named <hash>_<w>x<h>.mp4. Decoder opens the proxy
// when present+fresh; falls back to the source otherwise.
class ProxyManager : public QObject {
    Q_OBJECT
public:
    // Max long edge of a proxy. 960 keeps 16:9-ish sources at 960x540.
    static constexpr int kProxyLongEdge = 960;

    explicit ProxyManager(QObject *parent = nullptr);

    // Cache dir lives under QStandardPaths::CacheLocation/jooseclip/proxies.
    QString cacheDir() const;

    // Path of the proxy for a given source (whether or not it exists yet).
    QString proxyPath(const QString &sourcePath, int srcW, int srcH) const;

    // True when a fresh proxy exists (fresh = newer than source mtime).
    bool hasFreshProxy(const QString &sourcePath, int srcW, int srcH) const;

    // Which path a decoder should open for smooth playback.
    // Returns the proxy when fresh, else the source.
    QString playbackPath(const QString &sourcePath, int srcW, int srcH) const;

    // Build a proxy for the source (synchronous, potentially slow — call
    // from a worker). Uses system ffmpeg via QProcess. Emits done(ok,msg).
    bool build(const QString &sourcePath, int srcW, int srcH);

    // Remove all cached proxies (called from Preferences / on demand).
    qint64 clearAll();

signals:
    void proxyBuilt(const QString &sourcePath, const QString &proxyPath);
    void buildFailed(const QString &sourcePath, const QString &error);

private:
    QString dir_; // created in ctor
};