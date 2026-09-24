#pragma once

#include <QMainWindow>
#include <QImage>
#include <QTimer>
#include <QLabel>
#include <QHash>
#include <QCloseEvent>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include "decoder.h"
#include "timelinewidget.h"
#include "audioengine.h"
#include "proxymanager.h"

class QSlider;
class QThread;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

private slots:
    void importMedia();
    void importFolder();
    void openProject();
    void saveProject();
    void onPlayheadMoved(double t);
    void togglePlay();
    void razorAtPlayhead();
    void rippleDeleteAtPlayhead();
    void goPrevCut();
    void goNextCut();
    void undo();
    void redo();
    void zoomIn()  { zoomStep(1.3); }
    void zoomOut() { zoomStep(1.0 / 1.3); }
    void zoomStep(double f);
    void stepForward();   // one frame right (Premiere: Right / Shift+Right)
    void stepBackward();  // one frame left
    void shuttleBackward();  // J: play reverse (v0.2: step back at 2x tick)
    void shuttleForward();   // L: play faster forward (v0.2: 2x tick rate)
    void goStart();        // Home
    void goEnd();          // End
    void fitZoom();        // backslash: fit whole sequence in view
    void showExportDialog();
    void cancelExport();
    void updateTransportBar();
    void maybeBuildProxies();  // scan bin for un-proxied media, queue builds

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void renderFrameAt(double t);   // GUI: snapshot + queue async preview
    void previewWorkerLoop();       // worker: decode + fx + post to GUI
    void tick();
    Decoder *decoderFor(const QString &path);          // legacy GUI use (render)
    std::shared_ptr<Decoder> sharedDecoderFor(const QString &path); // thread-safe ref
    void clearDecoders();
    double frameDuration() const; // 1/fps of the clip at playhead (fallback 1/25)
    QString timecode(double t) const; // mm:ss:ff at timeline fps

    // async preview pipeline (GUI thread never decodes)
    struct PreviewReq {
        QString path;
        double local = 0.0;
        ClipEffects fx;
        qint64 id = 0;
        bool has = false;
    };
    PreviewReq previewPending_;      // latest requested frame (guarded by previewMutex_)
    std::mutex previewMutex_;
    std::condition_variable previewCv_;
    bool previewActive_ = false;    // worker running (guarded by previewMutex_)
    std::atomic<bool> previewAbort_{false};
    std::atomic<qint64> previewReqId_{0};
    class QThread *previewThread_ = nullptr;
    ClipEffects lastFx_;            // fx of the last delivered frame

    TimelineWidget *timeline_;
    QLabel *monitor_;
    QSlider *zoom_;
    QTimer playTimer_;
    QElapsedTimer clock_;
    QLabel *tcLabel_ = nullptr;    // transport bar timecode
    ProxyManager *proxies_ = nullptr;
    QThread *proxyThread_ = nullptr; // worker that builds proxies off-GUI
    int pendingProxies_ = 0;         // imports awaiting a proxy build
    QSet<QString> queuedForProxy_;   // paths with a build in flight
    AudioEngine *audio_ = nullptr;
    std::mutex audioSync_; // guards timeline vectors vs audio thread
    std::mutex regMutex_;  // guards the decoder registry (GUI + audio + workers)
    QByteArray defaultPanelState_; // baseline dock layout for Window > Reset
    double playingStartPlayhead_ = 0.0;
    bool playing_ = false;
    double lastFrameTime_ = -1.0;
    QString lastFramePath_;
    QImage lastImage_;
    QHash<QString, std::shared_ptr<Decoder>> decoders_;
};