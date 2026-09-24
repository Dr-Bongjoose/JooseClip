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

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void renderFrameAt(double t);
    void tick();
    Decoder *decoderFor(const QString &path);
    void clearDecoders();

    TimelineWidget *timeline_;
    QLabel *monitor_;
    QSlider *zoom_;
    QTimer playTimer_;
    QElapsedTimer clock_;
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