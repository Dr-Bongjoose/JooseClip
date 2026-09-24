#pragma once

#include <QDialog>
#include <QPointer>

#include "exporter.h"

class QLineEdit;
class QComboBox;
class QSpinBox;
class QProgressBar;
class QPushButton;
class QLabel;
class TimelineWidget;

// Export settings + progress dialog. Runs Exporter on a worker thread;
// progress streams into the bar; Cancel wires to Exporter::cancel().
class ExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportDialog(TimelineWidget *timeline, QWidget *parent = nullptr);

private slots:
    void browseOut();
    void startExport();
    void onCancel();

private:
    void buildUi();
    Exporter::Settings settingsFromUi() const;

    QPointer<TimelineWidget> timeline_;
    QLineEdit *pathEdit_ = nullptr;
    QComboBox *resBox_ = nullptr;
    QComboBox *fpsBox_ = nullptr;
    QSpinBox *vkbps_ = nullptr;
    QSpinBox *akbps_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QPushButton *startBtn_ = nullptr;
    QPushButton *cancelBtn_ = nullptr;
    QLabel *statusLbl_ = nullptr;
    QPointer<Exporter> exporter_;   // lives on the worker while running
    bool running_ = false;
};