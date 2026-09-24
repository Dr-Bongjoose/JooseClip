#include "mainwindow.h"
#include "medialibrary.h"

#include <QMenuBar>
#include <QDockWidget>
#include <QSplitter>
#include <QShortcut>
#include <QVBoxLayout>
#include <QApplication>
#include <QKeyEvent>
#include <QStatusBar>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileDialog>
#include <QFile>
#include <QStandardPaths>
#include <algorithm>

MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("JooseClip v0.1"));
    resize(1500, 900);

    // ---- Media bin (dock) ----
    auto *bin = new MediaLibrary(this);
    auto *binDock = new QDockWidget(QStringLiteral("Media Bin"), this);
    binDock->setWidget(bin);
    binDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    binDock->setObjectName(QStringLiteral("MediaBinDock")); // stable id for toggle/restore
    addDockWidget(Qt::LeftDockWidgetArea, binDock);

    // ---- Program monitor ----
    monitor_ = new QLabel(this);
    monitor_->setAlignment(Qt::AlignCenter);
    monitor_->setStyleSheet("background:#000;");
    monitor_->setMinimumSize(640, 360);

    // ---- Timeline ----
    timeline_ = new TimelineWidget(this);

    auto *center = new QWidget(this);
    auto *vlay = new QVBoxLayout(center);
    vlay->addWidget(monitor_, 3);
    vlay->addWidget(timeline_, 2);
    setCentralWidget(center);

    // ---- Menus ----
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Import Media…"), QKeySequence::Open, this, &MainWindow::importMedia);
    fileMenu->addAction(tr("&Save Project"), QKeySequence::Save, this, &MainWindow::saveProject);
    fileMenu->addAction(tr("&Open Project"), QKeySequence("Ctrl+O"), this, &MainWindow::openProject);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, qApp, &QCoreApplication::quit);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(tr("&Undo"), QKeySequence::Undo, this, &MainWindow::undo);
    editMenu->addAction(tr("&Redo"), QKeySequence::Redo, this, &MainWindow::redo);

    QMenu *clipMenu = menuBar()->addMenu(tr("&Clip"));
    clipMenu->addAction(tr("Razor at Playhead"), QKeySequence("C"), this, &MainWindow::razorAtPlayhead);
    clipMenu->addAction(tr("Ripple Delete at Playhead"), QKeySequence("Shift+Delete"), this, &MainWindow::rippleDeleteAtPlayhead);

    QMenu *markMenu = menuBar()->addMenu(tr("&Marks"));
    markMenu->addAction(tr("Go to Previous Cut"), QKeySequence("PgUp"), this, &MainWindow::goPrevCut);
    markMenu->addAction(tr("Go to Next Cut"), QKeySequence("PgDown"), this, &MainWindow::goNextCut);

    // Window menu: every dock/panel gets a toggle entry, created AFTER all
    // docks exist so nothing can be lost by closing it
    QMenu *windowMenu = menuBar()->addMenu(tr("&Window"));
    windowMenu->addAction(tr("Reset Panel Layout"), this, [this]() {
        restoreState(defaultPanelState_);
    });
    windowMenu->addSeparator();
    for (QDockWidget *dock : findChildren<QDockWidget*>()) {
        QAction *toggle = dock->toggleViewAction();
        toggle->setText(dock->windowTitle()); // human-readable panel name
        windowMenu->addAction(toggle);
    }
    defaultPanelState_ = saveState(1); // baseline for "Reset Panel Layout"

    // ---- Connections ----
    connect(timeline_, &TimelineWidget::playheadMoved, this, &MainWindow::onPlayheadMoved);
    connect(bin, &MediaLibrary::itemDropped, this, [this]() { statusBar()->showMessage(tr("Imported."), 2000); });

    playTimer_.setInterval(33); // ~30fps preview
    connect(&playTimer_, &QTimer::timeout, this, &MainWindow::tick);

    // audio
    audio_ = new AudioEngine(this);
    audio_->setTimeline(&timeline_->model.v1, &timeline_->model.v2);
    audio_->setSyncMutex(&audioSync_);
    audio_->setDecoderLookup([this](const QString &p) { return decoderFor(p); });

    // Premiere-style shortcuts.
    // NOTE: keys already attached to menu actions above (C, PgUp, PgDown,
    // Ctrl+Z, Ctrl+Shift+Z) must NOT get a second QShortcut — duplicate
    // sequences in one window become "ambiguous" and never fire.
    auto makeKey = [this](Qt::Key k, void (MainWindow::*slot)()) {
        auto *s = new QShortcut(QKeySequence(k), this);
        connect(s, &QShortcut::activated, this, slot);
        s->setAutoRepeat(false);
    };
    makeKey(Qt::Key_Space, &MainWindow::togglePlay);
    makeKey(Qt::Key_Delete, &MainWindow::rippleDeleteAtPlayhead);
    makeKey(Qt::Key_Plus, &MainWindow::zoomIn);
    makeKey(Qt::Key_Minus, &MainWindow::zoomOut);

    statusBar()->showMessage(tr("Drop media in the bin, drag to timeline. Space=play, C=razor, Shift+Del=ripple delete, Ctrl+Z/Ctrl+Shift+Z=undo/redo."));
}

void MainWindow::importMedia() {
    auto *bin = findChild<MediaLibrary*>();
    if (!bin) return;
    QStringList files = QFileDialog::getOpenFileNames(this, tr("Import Media"));
    for (const QString &f : files) bin->addPath(f);
}

void MainWindow::saveProject() {
    QString path = QFileDialog::getSaveFileName(this, tr("Save Project"), QString(),
                                                QStringLiteral("JooseClip project (*.jcproj.json)"));
    if (path.isEmpty()) return;
    QJsonArray arr;
    for (const auto &c : timeline_->model.v1) {
        QJsonObject o;
        o["path"] = c.path;
        o["start"] = c.timelineStart;
        o["srcIn"] = c.sourceIn;
        o["dur"] = c.duration;
        o["track"] = 1;
        QJsonObject fxo;
        fxo["brightness"] = c.fx.brightness;
        fxo["contrast"] = c.fx.contrast;
        fxo["saturation"] = c.fx.saturation;
        fxo["gamma"] = c.fx.gamma;
        o["fx"] = fxo;
        arr.append(o);
    }
    for (const auto &c : timeline_->model.v2) {
        QJsonObject o;
        o["path"] = c.path;
        o["start"] = c.timelineStart;
        o["srcIn"] = c.sourceIn;
        o["dur"] = c.duration;
        o["track"] = 2;
        QJsonObject fxo;
        fxo["brightness"] = c.fx.brightness;
        fxo["contrast"] = c.fx.contrast;
        fxo["saturation"] = c.fx.saturation;
        fxo["gamma"] = c.fx.gamma;
        o["fx"] = fxo;
        arr.append(o);
    }
    QJsonObject root;
    root["version"] = 1;
    root["clips"] = arr;
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(root).toJson());
    statusBar()->showMessage(tr("Project saved: %1").arg(path), 3000);
}

void MainWindow::openProject() {
    QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), QString(),
                                                QStringLiteral("JooseClip project (*.jcproj.json)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    timeline_->model.v1.clear();
    timeline_->model.v2.clear();
    timeline_->clearSelection();
    for (const QJsonValue &v : doc.object()["clips"].toArray()) {
        QJsonObject o = v.toObject();
        MediaInfo info = MediaLibrary::probe(o["path"].toString());
        if (!info.hasVideo && !info.hasAudio) continue;
        Clip c;
        c.path = info.path;
        c.info = info;
        c.sourceIn = o["srcIn"].toDouble();
        c.duration = o["dur"].toDouble();
        c.timelineStart = o["start"].toDouble();
        QJsonObject fxo = o["fx"].toObject();
        c.fx.brightness = fxo["brightness"].toDouble();
        c.fx.contrast = fxo["contrast"].toDouble(c.fx.contrast);
        c.fx.saturation = fxo["saturation"].toDouble(c.fx.saturation);
        c.fx.gamma = fxo["gamma"].toDouble(c.fx.gamma);
        c.fx.clamp();
        (o["track"].toInt() == 2 ? timeline_->model.v2 : timeline_->model.v1).append(c);
    }
    timeline_->model.sortTrack(timeline_->model.v1);
    timeline_->model.sortTrack(timeline_->model.v2);
    timeline_->update();
    statusBar()->showMessage(tr("Project loaded: %1").arg(path), 3000);
}

void MainWindow::onPlayheadMoved(double t) {
    renderFrameAt(t);
}

Decoder *MainWindow::decoderFor(const QString &path) {
    std::lock_guard<std::mutex> lock(audioSync_); // shared with audio thread
    auto it = decoders_.find(path);
    if (it != decoders_.end()) return *it;
    MediaInfo info = MediaLibrary::probe(path);
    auto *dec = new Decoder(info);
    decoders_[path] = dec;
    return dec;
}

void MainWindow::clearDecoders() {
    std::lock_guard<std::mutex> lock(audioSync_);
    qDeleteAll(decoders_);
    decoders_.clear();
}

void MainWindow::renderFrameAt(double t) {
    const Clip *clip = nullptr;
    int track = 0;
    // shared with audio thread: copy lookup data under the sync mutex
    QString path;
    double local = 0;
    {
        std::lock_guard<std::mutex> lock(audioSync_);
        if (!timeline_->model.clipAt(t, &clip, &track)) {
            monitor_->setText(tr("No clip at playhead"));
            lastFrameTime_ = -1;
            return;
        }
        path = clip->path;
        local = t - clip->timelineStart + clip->sourceIn;
    }
    // cache: same frame -> skip decode
    if (qAbs(local - lastFrameTime_) < 0.004 && !lastImage_.isNull() &&
        lastFramePath_ == path) return;
    Decoder *dec = decoderFor(path); // registry guarded by audioSync_ inside
    double pts = 0;
    QImage img = dec->frameAt(local, pts);
    if (!img.isNull()) {
        lastImage_ = img;
        lastFrameTime_ = local;
        lastFramePath_ = path;
        monitor_->setPixmap(QPixmap::fromImage(
            img.scaled(monitor_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
}

void MainWindow::togglePlay() {
    playing_ = !playing_;
    if (playing_) {
        playingStartPlayhead_ = timeline_->playhead();
        clock_.restart();
        playTimer_.start();
        audio_->playFrom(playingStartPlayhead_);
    } else {
        playTimer_.stop();
        audio_->stop();
    }
}

void MainWindow::tick() {
    double t;
    if (audio_->playing()) {
        // audio clock is master: video follows consumed samples (no drift)
        t = audio_->playheadSeconds();
    } else {
        t = playingStartPlayhead_ + clock_.elapsed() / 1000.0;
    }
    if (t > timeline_->model.sequenceEnd()) togglePlay();
    timeline_->setPlayhead(t);
    renderFrameAt(t);
}

void MainWindow::razorAtPlayhead() {
    timeline_->splitAt(timeline_->playhead());
}

void MainWindow::rippleDeleteAtPlayhead() {
    const Clip *clip = nullptr;
    int track = 0;
    if (!timeline_->model.clipAt(timeline_->playhead(), &clip, &track)) return;
    timeline_->rippleDelete(*clip);
}

void MainWindow::goPrevCut() {
    QVector<double> cuts {0.0};
    for (const auto &tr : {timeline_->model.v1, timeline_->model.v2})
        for (const auto &c : tr) {
            cuts << c.timelineStart << (c.timelineStart + c.duration);
        }
    std::sort(cuts.begin(), cuts.end());
    double t = timeline_->playhead() - 0.001;
    for (auto it = std::rbegin(cuts); it != std::rend(cuts); ++it) {
        if (*it < t) {
            timeline_->setPlayhead(*it);
            onPlayheadMoved(*it);
            return;
        }
    }
}

void MainWindow::goNextCut() {
    QVector<double> cuts;
    for (const auto &tr : {timeline_->model.v1, timeline_->model.v2})
        for (const auto &c : tr) {
            cuts << c.timelineStart << (c.timelineStart + c.duration);
        }
    std::sort(cuts.begin(), cuts.end());
    double t = timeline_->playhead() + 0.001;
    for (double c : cuts) {
        if (c > t) {
            timeline_->setPlayhead(c);
            onPlayheadMoved(c);
            return;
        }
    }
}

void MainWindow::undo() {
    if (!timeline_->undo()) {
        statusBar()->showMessage(tr("Nothing to undo."), 2000);
        return;
    }
    statusBar()->showMessage(tr("Undo."), 1500);
}

void MainWindow::redo() {
    if (!timeline_->redo()) {
        statusBar()->showMessage(tr("Nothing to redo."), 2000);
        return;
    }
    statusBar()->showMessage(tr("Redo."), 1500);
}

void MainWindow::closeEvent(QCloseEvent *e) {
    audio_->stop();
    clearDecoders();
    e->accept();
}

void MainWindow::zoomStep(double f) {
    // access pxPerSec_ via a small public helper would be cleaner;
    // for v0.1 we change zoom by re-setting it through a friend-ish call
    timeline_->setZoomFactor(timeline_->pxPerSec() * f);
}