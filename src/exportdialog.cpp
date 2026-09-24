#include "exportdialog.h"
#include "timelinewidget.h"
#include "theme.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QProgressBar>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>

ExportDialog::ExportDialog(TimelineWidget *timeline, QWidget *parent)
    : QDialog(parent), timeline_(timeline) {
    setWindowTitle(tr("Export"));
    setModal(true);
    buildUi();
    setMinimumWidth(460);
}

void ExportDialog::buildUi() {
    auto *lay = new QVBoxLayout(this);

    auto *form = new QFormLayout();
    pathEdit_ = new QLineEdit(this);
    pathEdit_->setText(QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).filePath("jooseclip_export.mp4"));
    auto *browse = new QPushButton(tr("Browse…"), this);
    connect(browse, &QPushButton::clicked, this, &ExportDialog::browseOut);
    auto *pathRow = new QHBoxLayout();
    pathRow->addWidget(pathEdit_, 1);
    pathRow->addWidget(browse);
    form->addRow(tr("Output file:"), pathRow);

    resBox_ = new QComboBox(this);
    resBox_->addItem("1920 x 1080 (1080p)");
    resBox_->addItem("1280 x 720 (720p)");
    resBox_->addItem("960 x 540 (540p)");
    resBox_->addItem("3840 x 2160 (2160p)");
    resBox_->setCurrentIndex(0);
    form->addRow(tr("Resolution:"), resBox_);

    fpsBox_ = new QComboBox(this);
    fpsBox_->addItem("30");
    fpsBox_->addItem("24");
    fpsBox_->addItem("25");
    fpsBox_->addItem("60");
    form->addRow(tr("Frame rate:"), fpsBox_);

    vkbps_ = new QSpinBox(this);
    vkbps_->setRange(500, 50000);
    vkbps_->setSingleStep(500);
    vkbps_->setValue(8000);
    vkbps_->setSuffix(" kbps");
    form->addRow(tr("Video bitrate:"), vkbps_);

    akbps_ = new QSpinBox(this);
    akbps_->setRange(64, 512);
    akbps_->setSingleStep(32);
    akbps_->setValue(192);
    akbps_->setSuffix(" kbps");
    form->addRow(tr("Audio bitrate:"), akbps_);

    lay->addLayout(form);

    bar_ = new QProgressBar(this);
    bar_->setRange(0, 100);
    bar_->setVisible(false);
    lay->addWidget(bar_);

    statusLbl_ = new QLabel(this);
    statusLbl_->setVisible(false);
    lay->addWidget(statusLbl_);

    auto *btns = new QHBoxLayout();
    btns->addStretch(1);
    cancelBtn_ = new QPushButton(tr("Cancel"), this);
    connect(cancelBtn_, &QPushButton::clicked, this, &ExportDialog::onCancel);
    startBtn_ = new QPushButton(tr("Export"), this);
    startBtn_->setDefault(true);
    connect(startBtn_, &QPushButton::clicked, this, &ExportDialog::startExport);
    btns->addWidget(cancelBtn_);
    btns->addWidget(startBtn_);
    lay->addLayout(btns);
}

void ExportDialog::browseOut() {
    QString p = QFileDialog::getSaveFileName(this, tr("Export to"),
        pathEdit_->text(), QStringLiteral("MP4 video (*.mp4)"));
    if (!p.isEmpty()) pathEdit_->setText(p);
}

Exporter::Settings ExportDialog::settingsFromUi() const {
    Exporter::Settings s;
    s.outPath = pathEdit_->text().trimmed();
    switch (resBox_->currentIndex()) {
        case 1: s.width = 1280; s.height = 720;  break;
        case 2: s.width = 960;  s.height = 540;  break;
        case 3: s.width = 3840; s.height = 2160; break;
        default: s.width = 1920; s.height = 1080; break;
    }
    s.fps = fpsBox_->currentText().toInt();
    s.videoKbps = vkbps_->value();
    s.audioKbps = akbps_->value();
    s.useEffects = true;
    return s;
}

void ExportDialog::startExport() {
    if (running_) return;
    Exporter::Settings s = settingsFromUi();
    if (s.outPath.isEmpty()) {
        QMessageBox::warning(this, tr("Export"), tr("Choose an output file."));
        return;
    }
    if (!timeline_ || timeline_->model.sequenceEnd() <= 0.0) {
        QMessageBox::warning(this, tr("Export"),
            tr("The timeline is empty — nothing to export."));
        return;
    }

    running_ = true;
    startBtn_->setEnabled(false);
    cancelBtn_->setText(tr("Cancel export"));
    bar_->setVisible(true);
    statusLbl_->setVisible(true);
    statusLbl_->setText(tr("Preparing…"));

    const qint64 total = Exporter::totalFrames(timeline_->model, s.fps);

    // Exporter runs on a worker thread; signals marshal back to the GUI.
    exporter_ = new Exporter;           // no parent: owned by worker context
    QThread *th = QThread::create([this, s]() {
        exporter_->run(timeline_->model, s);
    });
    connect(exporter_, &Exporter::progress, this, [this, total](qint64 done) {
        if (total > 0) bar_->setValue(int(done * 100 / total));
        statusLbl_->setText(tr("Frame %1").arg(done));
    });
    connect(exporter_, &Exporter::finished, this, [this, th](bool ok, const QString &msg) {
        running_ = false;
        bar_->setVisible(false);
        statusLbl_->setVisible(false);
        cancelBtn_->setText(tr("Close"));
        startBtn_->setEnabled(true);
        if (ok) {
            statusLbl_->setVisible(true);
            statusLbl_->setText(tr("Exported: %1").arg(msg));
            QMessageBox::information(this, tr("Export"),
                tr("Export finished:\n%1").arg(msg));
        } else {
            QMessageBox::warning(this, tr("Export"),
                tr("Export failed:\n%1").arg(msg));
        }
        th->quit();
    });
    connect(th, &QThread::finished, exporter_, &QObject::deleteLater);
    connect(th, &QThread::finished, th, &QObject::deleteLater);
    th->start();
}

void ExportDialog::onCancel() {
    if (running_) {
        if (exporter_) exporter_->cancel();
        statusLbl_->setText(tr("Cancelling…"));
        return; // finished() arrives and resets the UI
    }
    reject(); // not running: act as Close
}