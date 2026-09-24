#include "mainwindow.h"
#include "medialibrary.h"
#include "clipeffects.h"
#include "proxymanager.h"
#include "propertiespanel.h"

#include <QMenuBar>
#include <QDockWidget>
#include <QSplitter>
#include <QShortcut>
#include <QVBoxLayout>
#include <QPushButton>
#include <QHBoxLayout>
#include <QFont>
#include <QApplication>
#include <QKeyEvent>
#include <QStatusBar>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileDialog>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QThread>
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

    // ---- Effect Controls (dock) ----
    auto *props = new PropertiesPanel(timeline_, this);
    auto *propsDock = new QDockWidget(QStringLiteral("Effect Controls"), this);
    propsDock->setWidget(props);
    propsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    propsDock->setObjectName(QStringLiteral("EffectControlsDock"));
    addDockWidget(Qt::RightDockWidgetArea, propsDock);
    connect(props, &PropertiesPanel::effectsChanged, this, [this]() {
        // re-render the preview with the new fx values
        lastFrameTime_ = -1.0;
        renderFrameAt(timeline_->playhead());
    });

    // ---- Program monitor ----
    monitor_ = new QLabel(this);
    monitor_->setAlignment(Qt::AlignCenter);
    monitor_->setStyleSheet("background:#000;");
    monitor_->setMinimumSize(640, 360);

    // ---- Transport bar ----
    auto *transport = new QWidget(this);
    auto *tlay = new QHBoxLayout(transport);
    tlay->setContentsMargins(4, 2, 4, 2);
    tcLabel_ = new QLabel(QStringLiteral("00:00:00 / 00:00:00"), transport);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::TypeWriter);
    tcLabel_->setFont(mono);
    tcLabel_->setMinimumWidth(160);
    auto *playBtn = new QPushButton(tr("Play"), transport);
    playBtn->setFixedWidth(90);
    connect(playBtn, &QPushButton::clicked, this, &MainWindow::togglePlay);
    tlay->addWidget(tcLabel_);
    tlay->addStretch(1);
    tlay->addWidget(playBtn);

    // ---- Timeline ----
    timeline_ = new TimelineWidget(this);
    // strips (filmstrip/waveform) decode through proxies when available
    timeline_->setPlaybackPathResolver([this](const QString &p) {
        MediaInfo info = MediaLibrary::probe(p);
        if (proxies_ && info.hasVideo && info.width > 0 &&
            proxies_->hasFreshProxy(p, info.width, info.height))
            return proxies_->playbackPath(p, info.width, info.height);
        return p;
    });

    auto *center = new QWidget(this);
    auto *vlay = new QVBoxLayout(center);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->addWidget(monitor_, 3);
    vlay->addWidget(transport);
    vlay->addWidget(timeline_, 2);
    setCentralWidget(center);

    // ---- Menus ----
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Import Media…"), QKeySequence::Open, this, &MainWindow::importMedia);
    QAction *folderAct = fileMenu->addAction(tr("Import F&older…"), this, &MainWindow::importFolder);
    folderAct->setShortcut(QKeySequence("Ctrl+Shift+I"));
    fileMenu->addAction(tr("&Save Project"), QKeySequence::Save, this, &MainWindow::saveProject);
    fileMenu->addAction(tr("&Open Project"), QKeySequence("Ctrl+O"), this, &MainWindow::openProject);
    fileMenu->addSeparator();
    QAction *exportAct = fileMenu->addAction(tr("&Export…"), this, &MainWindow::showExportDialog);
    exportAct->setShortcut(QKeySequence("Ctrl+M"));
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

    // ---- Proxy pipeline: build 960p proxies off-thread on import ----
    proxies_ = new ProxyManager(this);
    connect(bin, &MediaLibrary::itemDropped, this, &MainWindow::maybeBuildProxies);
    connect(bin, &MediaLibrary::itemDropped, this, [this]() {
        if (pendingProxies_ > 0)
            statusBar()->showMessage(tr("Building playback proxies… %1 queued").arg(pendingProxies_), 3000);
    });

    playTimer_.setInterval(33); // ~30fps preview
    connect(&playTimer_, &QTimer::timeout, this, &MainWindow::tick);

    // audio
    audio_ = new AudioEngine(this);
    audio_->setTimeline(&timeline_->model.v1, &timeline_->model.v2);
    audio_->setSyncMutex(&audioSync_);
    audio_->setDecoderLookup([this](const QString &p) { return decoderFor(p); });

    // JKL + frame stepping (Premiere-style transport)
    auto makeSeqKey = [this](const QKeySequence &ks, void (MainWindow::*slot)()) {
        auto *s = new QShortcut(ks, this);
        connect(s, &QShortcut::activated, this, slot);
        s->setAutoRepeat(false);
        return s;
    };
    makeSeqKey(Qt::Key_J, &MainWindow::shuttleBackward);
    makeSeqKey(Qt::Key_L, &MainWindow::shuttleForward);
    makeSeqKey(Qt::Key_Home, &MainWindow::goStart);
    makeSeqKey(Qt::Key_End, &MainWindow::goEnd);
    makeSeqKey(QKeySequence("\\"), &MainWindow::fitZoom);
    // frame step: auto-repeat OK
    {
        auto *s = new QShortcut(QKeySequence(Qt::Key_Right), this);
        connect(s, &QShortcut::activated, this, &MainWindow::stepForward);
    }
    {
        auto *s = new QShortcut(QKeySequence(Qt::Key_Left), this);
        connect(s, &QShortcut::activated, this, &MainWindow::stepBackward);
    }

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
    updateTransportBar();
}

void MainWindow::maybeBuildProxies() {
    auto *bin = findChild<MediaLibrary*>();
    if (!bin || !proxies_) return;
    // Collect un-proxied video files from the bin
    QStringList queue;
    for (int i = 0; i < bin->count(); ++i) {
        QListWidgetItem *item = bin->item(i);
        MediaInfo info = bin->infoFor(item);
        if (!info.hasVideo || info.width <= 0) continue;
        if (proxies_->hasFreshProxy(info.path, info.width, info.height)) continue;
        if (queuedForProxy_.contains(info.path)) continue;
        queue << info.path;
        queuedForProxy_.insert(info.path);
    }
    if (queue.isEmpty()) return;
    pendingProxies_ += queue.size();

    // One worker thread per import batch; ProxyManager::build() blocks there.
    const QStringList paths = queue;
    auto *thread = QThread::create([this, paths]() {
        for (const QString &p : paths) {
            MediaInfo info = MediaLibrary::probe(p);
            if (!info.hasVideo || info.width <= 0) continue;
            proxies_->build(p, info.width, info.height);
        }
    });
    connect(thread, &QThread::finished, this, [this, paths]() {
        pendingProxies_ = qMax(0, pendingProxies_ - paths.size());
        for (const QString &p : paths) queuedForProxy_.remove(p);
        // decoders may now resolve to proxies: drop stale full-res ones
        clearDecoders();
        statusBar()->showMessage(tr("Playback proxies ready."), 3000);
    });
    thread->start();
}

Decoder *MainWindow::decoderFor(const QString &path) {
    std::lock_guard<std::mutex> lock(audioSync_); // shared with audio thread
    // Registry keys by SOURCE path (stable identity); the Decoder opens the
    // proxy when a fresh one exists — that's what makes high-res sources
    // playable in the VM. Decoder re-probes all metadata from whatever file
    // it opens; clip timing stays driven by the source-based model.
    auto it = decoders_.find(path);
    if (it != decoders_.end()) return *it;
    MediaInfo info = MediaLibrary::probe(path);
    MediaInfo openInfo = info;
    if (proxies_ && info.hasVideo && info.width > 0 &&
        proxies_->hasFreshProxy(path, info.width, info.height)) {
        openInfo.path = proxies_->playbackPath(path, info.width, info.height);
    }
    auto *dec = new Decoder(openInfo);
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
        // apply the clip's color effects to the preview
        if (!clip->fx.isIdentity()) applyEffects(img, clip->fx);
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

double MainWindow::frameDuration() const {
    const Clip *c = nullptr;
    int track = 0;
    if (timeline_->model.clipAt(timeline_->playhead(), &c, &track) && c && c->info.fps > 0)
        return 1.0 / c->info.fps;
    return 1.0 / 25.0;
}

void MainWindow::stepForward() {
    if (playing_) togglePlay(); // pause before frame-stepping
    double t = timeline_->playhead() + frameDuration();
    timeline_->setPlayhead(t);
    onPlayheadMoved(t);
}

void MainWindow::stepBackward() {
    if (playing_) togglePlay();
    double t = qMax(0.0, timeline_->playhead() - frameDuration());
    timeline_->setPlayhead(t);
    onPlayheadMoved(t);
}

void MainWindow::shuttleBackward() {
    // J: reverse. v0.2 semantics: pause, then step back a frame.
    if (playing_) togglePlay();
    stepBackward();
}

void MainWindow::shuttleForward() {
    // L: fast forward. v0.2 semantics: pause, then step forward.
    if (playing_) togglePlay();
    stepForward();
}

void MainWindow::goStart() {
    if (playing_) togglePlay();
    timeline_->setPlayhead(0.0);
    onPlayheadMoved(0.0);
}

void MainWindow::goEnd() {
    if (playing_) togglePlay();
    double end = timeline_->model.sequenceEnd();
    timeline_->setPlayhead(end);
    onPlayheadMoved(end);
}

void MainWindow::fitZoom() {
    double end = timeline_->model.sequenceEnd();
    if (end <= 0) return;
    double pps = (double(timeline_->width()) - 80.0) / end;
    timeline_->setZoomFactor(qMax(2.0, pps));
}

QString MainWindow::timecode(double t) const {
    double fps = 25.0;
    const Clip *c = nullptr;
    int track = 0;
    if (timeline_->model.clipAt(t, &c, &track) && c && c->info.fps > 0)
        fps = c->info.fps;
    int totalFrames = int(t * fps + 0.5);
    int frames = totalFrames % int(fps);
    int secs = int(t);
    int mins = secs / 60;
    secs %= 60;
    return QString("%1:%2:%3")
        .arg(mins, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'))
        .arg(frames, 2, 10, QChar('0'));
}

void MainWindow::updateTransportBar() {
    if (tcLabel_)
        tcLabel_->setText(timecode(timeline_->playhead()) +
                          QStringLiteral(" / ") + timecode(timeline_->model.sequenceEnd()));
}

void MainWindow::importFolder() {
    auto *bin = findChild<MediaLibrary*>();
    if (!bin) return;
    QString dir = QFileDialog::getExistingDirectory(this, tr("Import Folder"));
    if (dir.isEmpty()) return;
    QStringList exts = {"mp4", "mov", "mkv", "avi", "webm", "mp3", "wav", "m4a", "flac", "aac"};
    int n = 0;
    QDir d(dir);
    const auto entries = d.entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &fi : entries) {
        if (exts.contains(fi.suffix().toLower())) {
            bin->addPath(fi.absoluteFilePath());
            ++n;
        }
    }
    statusBar()->showMessage(tr("Imported %1 file(s) from folder.").arg(n), 3000);
}

void MainWindow::showExportDialog() {
    // Placeholder until the Exporter module lands; wiring comes with the merge.
    statusBar()->showMessage(tr("Export module integration pending."), 3000);
}

void MainWindow::cancelExport() {}