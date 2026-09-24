#include "timelinewidget.h"

#include <QPainter>
#include <QMimeData>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include "medialibrary.h"
#include "theme.h"
#include <algorithm>

namespace {
const int kRulerH = 26;
const int kTrackH = 64;
const int kTracksTop = 0;
const QColor kV1Color(0x3a, 0x5f, 0x8a);
const QColor kV2Color(0x4a, 0x7a, 0x5a);
const QColor kRulerColor(0x1a, 0x1a, 0x1e);
}

TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent) {
    setAcceptDrops(true);
    setMouseTracking(true);
    setMinimumHeight(200);
}

int TimelineWidget::trackAt(int y) const {
    if (y < kRulerH) return -1;
    int row = (y - kRulerH) / kTrackH;
    if (row == 0) return 1;  // V2 on top
    if (row == 1) return 0;  // V1 below
    return -2;
}

double TimelineWidget::xToTime(int x) const { return double(x) / pxPerSec_; }
int TimelineWidget::timeToX(double t) const { return int(t * pxPerSec_); }

double TimelineWidget::timelineEnd() const { return model.sequenceEnd(); }

// ---- undo/redo -----------------------------------------------------------

void TimelineWidget::pushUndo() {
    ModelState st;
    st.v1 = model.v1;
    st.v2 = model.v2;
    if (undoStack_.isEmpty() || !(undoStack_.last() == st)) {
        undoStack_.append(st);
        while (undoStack_.size() > kMaxUndo) undoStack_.removeFirst();
        redoStack_.clear();
        emit undoStateChanged();
    }
}

bool TimelineWidget::undo() {
    if (undoStack_.isEmpty()) return false;
    ModelState cur;
    cur.v1 = model.v1;
    cur.v2 = model.v2;
    redoStack_.append(cur);

    ModelState st = undoStack_.takeLast();
    model.v1 = st.v1;
    model.v2 = st.v2;
    clearSelection(); // indexes are stale after a model swap
    update();
    emit modelChanged();
    emit undoStateChanged();
    return true;
}

bool TimelineWidget::redo() {
    if (redoStack_.isEmpty()) return false;
    ModelState cur;
    cur.v1 = model.v1;
    cur.v2 = model.v2;
    undoStack_.append(cur);

    ModelState st = redoStack_.takeLast();
    model.v1 = st.v1;
    model.v2 = st.v2;
    clearSelection(); // indexes are stale after a model swap
    update();
    emit modelChanged();
    emit undoStateChanged();
    return true;
}

void TimelineWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0x10, 0x10, 0x12));

    // Ruler
    p.fillRect(0, 0, width(), kRulerH, kRulerColor);
    double step = 1.0;
    // pick tick spacing so labels don't overlap
    while (step * pxPerSec_ < 70) step *= (step < 1 ? 5 : 2);
    p.setPen(QColor(0x90, 0x90, 0x90));
    QFont f = p.font();
    f.setPixelSize(10);
    p.setFont(f);
    for (double t = 0; t < xToTime(width()); t += step) {
        int x = timeToX(t);
        p.drawLine(x, kRulerH - 6, x, kRulerH);
        QString lbl;
        if (step < 1) {
            int secs = int(t);
            int frames = int((t - secs) * qMax(1.0, fpsForRuler()));
            lbl = QString("%1:%02d").arg(secs).arg(frames);
        } else {
            lbl = QString("%1:%02d").arg(int(t / 60)).arg(int(t) % 60);
        }
        p.drawText(x + 3, kRulerH - 9, lbl);
    }

    // Tracks: V2 row then V1 row
    for (int ti = 1; ti >= 0; --ti) {
        int y = kRulerH + (ti == 0 ? kTrackH : 0);
        QColor bg = (ti == 1) ? QColor(0x14, 0x1a, 0x14) : QColor(0x12, 0x14, 0x1a);
        p.fillRect(0, y, width(), kTrackH, bg);
        const QVector<Clip> &track = ti ? model.v2 : model.v1;
        for (int ci = 0; ci < track.size(); ++ci) {
            const Clip &c = track[ci];
            int x1 = timeToX(c.timelineStart);
            int x2 = timeToX(c.timelineStart + c.duration);
            QRect r(x1, y + 4, qMax(4, x2 - x1), kTrackH - 8);
            p.setPen(QColor(0x22, 0x22, 0x22));
            p.setBrush(ti ? Theme::trackV2() : Theme::trackV1());
            p.drawRoundedRect(r, 3, 3);
            // selection highlight (gold outline, 2px)
            if (selTrack_ == ti && selIndex_ == ci) {
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(Theme::gold(), 2));
                p.drawRoundedRect(r.adjusted(-1, -1, 1, 1), 3, 3);
            }
            p.setPen(Qt::white);
            p.drawText(r.adjusted(4, 2, -4, -2), Qt::AlignTop | Qt::AlignLeft,
                       QFileInfo(c.path).fileName());
        }
    }

    // Track labels
    p.setPen(QColor(0x77, 0x88, 0x99));
    p.drawText(4, kRulerH + 14, "V2");
    p.drawText(4, kRulerH + kTrackH + 14, "V1");

    // Playhead
    int px = timeToX(playhead_);
    p.setPen(QPen(QColor(0xff, 0x50, 0x50), 2));
    p.drawLine(px, 0, px, kRulerH + 2 * kTrackH);
    QPolygon tri;
    tri << QPoint(px - 6, 0) << QPoint(px + 6, 0) << QPoint(px, 10);
    p.setBrush(Qt::red);
    p.setPen(Qt::NoPen);
    p.drawPolygon(tri);
}

double TimelineWidget::fpsForRuler() const {
    for (const auto &c : model.v1) if (c.info.fps > 0) return c.info.fps;
    for (const auto &c : model.v2) if (c.info.fps > 0) return c.info.fps;
    return 25.0;
}

double TimelineWidget::snapTime(double t, double ignoreStart, double ignoreEnd) {
    // collect candidate snap points: clip edges on both tracks + 0
    QVector<double> pts;
    pts << 0.0;
    for (const auto &tr : {model.v1, model.v2}) {
        for (const auto &c : tr) {
            if (c.timelineStart + c.duration <= ignoreStart ||
                c.timelineStart >= ignoreEnd) {
                pts << c.timelineStart << (c.timelineStart + c.duration);
            }
        }
    }
    double best = t;
    double bestDist = snapPx_ / pxPerSec_;
    for (double c : pts) {
        double d = qAbs(c - t);
        if (d < bestDist) { bestDist = d; best = c; }
    }
    return best;
}

int TimelineWidget::clipIndexAt(QVector<Clip> &track, double t, int *edge) {
    for (int i = 0; i < track.size(); ++i) {
        const Clip &c = track[i];
        double end = c.timelineStart + c.duration;
        if (t >= c.timelineStart && t <= end) {
            if (edge) *edge = (t * pxPerSec_ - timeToX(c.timelineStart) < 5) ? -1
                            : (timeToX(end) - t * pxPerSec_ < 5) ? 1 : 0;
            return i;
        }
    }
    return -1;
}

void TimelineWidget::pushDownClips(QVector<Clip> &track, double from, double gap) {
    for (Clip &c : track)
        if (c.timelineStart >= from - 1e-6)
            c.timelineStart -= gap;
    model.sortTrack(track);
}

void TimelineWidget::pushRightClips(QVector<Clip> &track, double from, double gap) {
    for (Clip &c : track)
        if (c.timelineStart >= from - 1e-6)
            c.timelineStart += gap;
    model.sortTrack(track);
}

void TimelineWidget::mousePressEvent(QMouseEvent *e) {
    int track = trackAt(e->position().toPoint().y());
    double t = xToTime(int(e->position().x()));

    if (e->button() == Qt::LeftButton && track == -1) {
        // ruler scrub
        mode_ = Mode::Scrub;
        playhead_ = qMax(0.0, t);
        emit playheadMoved(playhead_);
        update();
        return;
    }
    if (e->button() != Qt::LeftButton || track < 0) {
        mode_ = Mode::None;
        return;
    }

    QVector<Clip> &tr = track ? model.v2 : model.v1;
    int edge = 0;
    int idx = clipIndexAt(tr, t, &edge);

    if (idx >= 0 && edge != 0) {
        // trim
        mode_ = (edge < 0) ? Mode::TrimStart : Mode::TrimEnd;
        dragTrack_ = track;
        dragIndex_ = idx;
        dragChanged_ = false;
        pushUndo();
        return;
    }
    if (idx >= 0) {
        // move + select
        mode_ = Mode::MoveClip;
        dragTrack_ = track;
        dragIndex_ = idx;
        dragGrabOffset_ = t - tr[idx].timelineStart;
        dragChanged_ = false;
        selTrack_ = track;
        selIndex_ = idx;
        emit selectionChanged();
        pushUndo();
        update();
        return;
    }
    // click on empty track space: clear selection
    clearSelection();
    mode_ = Mode::None;
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *e) {
    double t = xToTime(int(e->position().x()));
    int track = trackAt(e->position().toPoint().y());

    switch (mode_) {
    case Mode::Scrub:
        playhead_ = qMax(0.0, t);
        emit playheadMoved(playhead_);
        update();
        break;
    case Mode::MoveClip: {
        QVector<Clip> &tr = dragTrack_ ? model.v2 : model.v1;
        Clip &c = tr[dragIndex_];
        double newStart = qMax(0.0, t - dragGrabOffset_);
        // snapping: to other clips' edges
        double snapped = newStart;
        double snapTol = snapPx_ / pxPerSec_;
        for (const auto &o : tr) {
            if (&o == &c) continue;
            for (double cand : {o.timelineStart, o.timelineStart + o.duration,
                                o.timelineStart - c.duration}) {
                if (qAbs(cand - newStart) < snapTol) snapped = cand;
            }
        }
        if (!qFuzzyCompare(snapped, c.timelineStart)) dragChanged_ = true;
        c.timelineStart = qMax(0.0, snapped);
        update();
        break;
    }
    case Mode::TrimStart: {
        QVector<Clip> &tr = dragTrack_ ? model.v2 : model.v1;
        Clip &c = tr[dragIndex_];
        double delta = t - c.timelineStart;
        if (delta > 0 && delta < c.duration - 0.05) {
            dragChanged_ = true;
            c.timelineStart += delta;
            c.duration -= delta;
            c.sourceIn += delta;
        }
        update();
        break;
    }
    case Mode::TrimEnd: {
        QVector<Clip> &tr = dragTrack_ ? model.v2 : model.v1;
        Clip &c = tr[dragIndex_];
        double newEnd = qMin(c.info.duration - c.sourceIn + c.timelineStart, t);
        if (newEnd > c.timelineStart + 0.05) {
            dragChanged_ = true;
            c.duration = newEnd - c.timelineStart;
        }
        update();
        break;
    }
    default:
        // hover cursor feedback
        if (track >= 0) {
            QVector<Clip> &tr = track ? model.v2 : model.v1;
            int edge = 0;
            int idx = clipIndexAt(tr, t, &edge);
            setCursor(idx >= 0 && edge != 0 ? Qt::SizeHorCursor : Qt::ArrowCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
        break;
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *) {
    // drag ended without mutating the model: drop the snapshot pushed on
    // press so a plain click doesn't create a phantom undo step
    if ((mode_ == Mode::MoveClip || mode_ == Mode::TrimStart || mode_ == Mode::TrimEnd) &&
        !dragChanged_ && !undoStack_.isEmpty()) {
        ModelState st;
        st.v1 = model.v1;
        st.v2 = model.v2;
        if (undoStack_.last() == st) {
            undoStack_.removeLast();
            emit undoStateChanged();
        }
    }
    mode_ = Mode::None;
    dragChanged_ = false;
    emit modelChanged();
}

void TimelineWidget::wheelEvent(QWheelEvent *e) {
    if (e->modifiers() & Qt::ControlModifier) {
        // zoom centered on playhead (v0.1 keeps left-anchored view)
        double factor = e->angleDelta().y() > 0 ? 1.25 : 0.8;
        pxPerSec_ = qBound(2.0, pxPerSec_ * factor, 400.0);
        update();
    } else {
        e->ignore();
    }
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent *e) {
    // double-click empty space: move playhead there (convenience)
    int track = trackAt(e->position().toPoint().y());
    if (track == -1) {
        playhead_ = qMax(0.0, xToTime(int(e->position().x())));
        emit playheadMoved(playhead_);
        update();
    }
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasText() || e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent *e) {
    // Qt only fires dropEvent if dragMove accepts the position.
    if (e->mimeData()->hasText() || e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void TimelineWidget::dropEvent(QDropEvent *e) {
    QString path = e->mimeData()->text();
    if (path.isEmpty()) {
        for (const QUrl &u : e->mimeData()->urls()) { path = u.toLocalFile(); break; }
    }
    if (path.isEmpty()) return;

    MediaInfo info = MediaLibrary::probe(path);
    if (!info.hasVideo && !info.hasAudio) return;

    int track = trackAt(e->position().toPoint().y());
    if (track < 0) track = 0;

    pushUndo();
    Clip c;
    c.path = info.path;
    c.info = info;
    c.sourceIn = 0.0;
    c.duration = info.duration;
    c.timelineStart = snapTime(qMax(0.0, xToTime(int(e->position().x()))), 0, 0);
    addClipToTrack(track, c, true);
    e->acceptProposedAction();
    emit modelChanged();
}

void TimelineWidget::addClipToTrack(int track, const Clip &c, bool pushDown) {
    QVector<Clip> &tr = track ? model.v2 : model.v1;
    tr.append(c);
    model.sortTrack(tr);
    if (pushDown) {
        // overwrite-style: clip lands and pushes overlapping clips right
        double end = c.timelineStart + c.duration;
        for (Clip &o : tr) {
            if (&o == &c) continue;
            if (o.timelineStart < end && o.timelineStart + o.duration > c.timelineStart) {
                double shift = end - o.timelineStart;
                o.timelineStart += shift;
            }
        }
        model.sortTrack(tr);
    }
    update();
}

void TimelineWidget::rippleDelete(const Clip &target) {
    for (int ti = 0; ti < 2; ++ti) {
        QVector<Clip> &tr = ti ? model.v2 : model.v1;
        for (int i = 0; i < tr.size(); ++i) {
            if (&tr[i] == &target) {
                pushUndo();
                double gap = target.duration;
                tr.removeAt(i);
                pushRightClips(tr, target.timelineStart, -gap);
                if (selTrack_ == ti && selIndex_ == i) clearSelection();
                else if (selTrack_ == ti && selIndex_ > i) --selIndex_;
                update();
                emit modelChanged();
                emit selectionChanged();
                return;
            }
        }
    }
}

void TimelineWidget::splitAt(double t) {
    for (int ti = 0; ti < 2; ++ti) {
        QVector<Clip> &tr = ti ? model.v2 : model.v1;
        for (int i = 0; i < tr.size(); ++i) {
            const Clip &c = tr[i]; // read-only: mutation below must go through copies
            double end = c.timelineStart + c.duration;
            if (t > c.timelineStart + 0.01 && t < end - 0.01) {
                pushUndo(); // snapshot BEFORE any mutation (COW-safe)
                Clip left = tr[i];
                Clip right = left;
                double cut = t - left.timelineStart;
                right.timelineStart = t;
                right.sourceIn = left.sourceIn + cut;
                right.duration = end - t;
                left.duration = cut;
                tr[i] = left;       // non-const op[] -> detaches from snapshot
                tr.insert(i + 1, right);
                // selection: keep pointing at the left part of the split
                if (selTrack_ == ti) {
                    if (selIndex_ == i) { /* stays on left */ }
                    else if (selIndex_ > i) ++selIndex_; // shifted right by insert
                }
                update();
                emit modelChanged();
                emit selectionChanged();
                return;
            }
        }
    }
}

// ---- selection ------------------------------------------------------------

const Clip *TimelineWidget::selectedClip() const {
    if (selTrack_ < 0 || selTrack_ > 1) return nullptr;
    const QVector<Clip> &tr = selTrack_ ? model.v2 : model.v1;
    if (selIndex_ < 0 || selIndex_ >= tr.size()) return nullptr;
    return &tr[selIndex_];
}

bool TimelineWidget::selectAt(double t) {
    const Clip *c = nullptr;
    int track = 0;
    if (!model.clipAt(t, &c, &track)) { clearSelection(); return false; }
    const QVector<Clip> &tr = track ? model.v2 : model.v1;
    for (int i = 0; i < tr.size(); ++i)
        if (&tr[i] == c) {
            if (selTrack_ != track || selIndex_ != i) {
                selTrack_ = track; selIndex_ = i;
                update();
                emit selectionChanged();
                return true;
            }
            return false;
        }
    return false;
}

void TimelineWidget::clearSelection() {
    if (selTrack_ < 0) return;
    selTrack_ = -1;
    selIndex_ = -1;
    update();
    emit selectionChanged();
}

bool TimelineWidget::setEffectsOnSelected(const ClipEffects &fx) {
    if (selTrack_ < 0 || selIndex_ < 0) return false;
    QVector<Clip> &tr = selTrack_ ? model.v2 : model.v1;
    if (selIndex_ >= tr.size()) return false;
    if (tr[selIndex_].fx == fx) return false; // no change, no undo push
    pushUndo();
    tr[selIndex_].fx = fx;
    update();
    emit modelChanged();
    emit selectionChanged();
    return true;
}