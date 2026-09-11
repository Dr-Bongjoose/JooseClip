#pragma once

#include <QListWidget>
#include "decoder.h"

class MediaLibrary : public QListWidget {
    Q_OBJECT
public:
    explicit MediaLibrary(QWidget *parent = nullptr);

    // Probe file with FFmpeg; returns info or invalid entry on failure.
    static MediaInfo probe(const QString &path);

    MediaInfo infoFor(QListWidgetItem *item) const;
    void addPath(const QString &path);

signals:
    void itemDropped(); // after external files were dropped in

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void startDrag(Qt::DropActions) override;

private:
    QHash<QListWidgetItem*, MediaInfo> infos_;
};