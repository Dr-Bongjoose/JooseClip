#include "medialibrary.h"

#include <QFileInfo>
#include <QMimeData>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>

MediaLibrary::MediaLibrary(QWidget *parent) : QListWidget(parent) {
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
}

MediaInfo MediaLibrary::probe(const QString &path) {
    MediaInfo info;
    info.path = path;
    info.name = QFileInfo(path).fileName();
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return info;
    avformat_find_stream_info(fmt, nullptr);
    if (fmt->duration != AV_NOPTS_VALUE)
        info.duration = fmt->duration / double(AV_TIME_BASE);

    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        AVStream *st = fmt->streams[i];
        AVCodecParameters *p = st->codecpar;
        if (p->codec_type == AVMEDIA_TYPE_VIDEO && !info.hasVideo) {
            info.hasVideo = true;
            info.width = p->width;
            info.height = p->height;
            AVRational fr = av_guess_frame_rate(fmt, st, nullptr);
            if (fr.num > 0 && fr.den > 0)
                info.fps = av_q2d(fr);
        } else if (p->codec_type == AVMEDIA_TYPE_AUDIO && !info.hasAudio) {
            info.hasAudio = true;
        }
    }
    avformat_close_input(&fmt);
    return info;
}

MediaInfo MediaLibrary::infoFor(QListWidgetItem *item) const {
    return infos_.value(item, MediaInfo());
}

void MediaLibrary::addPath(const QString &path) {
    MediaInfo info = probe(path);
    if (!info.hasVideo && !info.hasAudio) return;
    auto *item = new QListWidgetItem(this);
    item->setText(info.name);
    item->setData(Qt::UserRole, info.path);
    item->setToolTip(QStringLiteral("%1  %2x%3  %4fps  %5s")
                         .arg(info.name).arg(info.width).arg(info.height)
                         .arg(info.fps, 0, 'f', 2).arg(info.duration, 0, 'f', 2));
    infos_[item] = info;
}

void MediaLibrary::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MediaLibrary::dragMoveEvent(QDragMoveEvent *e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MediaLibrary::dropEvent(QDropEvent *e) {
    for (const QUrl &u : e->mimeData()->urls()) {
        QString p = u.toLocalFile();
        if (!p.isEmpty()) addPath(p);
    }
    e->acceptProposedAction();
    emit itemDropped();
}

void MediaLibrary::startDrag(Qt::DropActions) {
    const QList<QListWidgetItem*> items = selectedItems();
    if (items.isEmpty()) return;

    QList<QUrl> urls;
    QStringList paths;
    for (QListWidgetItem *item : items) {
        MediaInfo info = infos_.value(item, MediaInfo());
        if (info.path.isEmpty()) continue;
        urls << QUrl::fromLocalFile(info.path);
        paths << info.path;
    }
    if (urls.isEmpty()) return;

    auto *mime = new QMimeData;
    mime->setUrls(urls);
    mime->setText(paths.join(QLatin1Char('\n')));

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(Qt::CopyAction);
}