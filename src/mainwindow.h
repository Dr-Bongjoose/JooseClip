#pragma once

#include <QMainWindow>
#include <QImage>
#include <QTimer>
#include <QLabel>
#include <QHash>
#include <QCloseEvent>
#include <mutex>
#include "decoder.h"
#include "timelinewidget.h"
#include "audioengine.h"

class QSlider;

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

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void renderFrameAt(double t);
    void tick();
    Decoder *decoderFor(const QString &path);
    void clearDecoders();
    double frameDuration() const; // 1/fps of the clip at playhead (fallback 1/25)
    QString timecode(double t) const; // mm:ss:ff at timeline fps

    TimelineWidget *timeline_;
    QLabel *monitor_;
    QSlider *zoom_;
    QTimer playTimer_;
    QElapsedTimer clock_;
    QLabel *tcLabel_ = nullptr;    // transport bar timecode
    AudioEngine *audio_ = nullptr;
    std::mutex audioSync_; // guards timeline vectors + decoder registry vs audio thread
    QByteArray defaultPanelState_; // baseline dock layout for Window > Reset
    double playingStartPlayhead_ = 0.0;
    bool playing_ = false;
    double lastFrameTime_ = -1.0;
    QString lastFramePath_;
    QImage lastImage_;
    QHash<QString, Decoder*> decoders_;
};