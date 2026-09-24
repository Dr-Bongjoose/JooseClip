#pragma once

#include <QImage>
#include <QWidget>
#include <QVector>
#include <QSet>
#include <functional>

#include "decoder.h"
#include "clipeffects.h"

struct Clip {
    QString path;       // source media path
    MediaInfo info;     // probed metadata
    double sourceIn = 0.0;   // seconds into source
    double timelineStart = 0.0; // seconds on timeline
    double duration = 0.0;      // timeline duration (== source length for v1)
    ClipEffects fx;            // per-clip color effects (identity = none)

    bool operator==(const Clip &o) const {
        return path == o.path && sourceIn == o.sourceIn &&
               timelineStart == o.timelineStart && duration == o.duration &&
               fx == o.fx;
    }
};

// Timeline model: V1 = main video track, V2 = overlay track, plus
// derived "A1" audio conceptually under V1 (audio playback is v2).
class TimelineModel {
public:
    QVector<Clip> v1;
    QVector<Clip> v2;

    void sortTrack(QVector<Clip> &track) const {
        std::sort(track.begin(), track.end(),
                  [](const Clip &a, const Clip &b) {
                      return a.timelineStart < b.timelineStart;
                  });
    }
    double sequenceEnd() const {
        double e = 0;
        for (const auto &c : v1) e = qMax(e, c.timelineStart + c.duration);
        for (const auto &c : v2) e = qMax(e, c.timelineStart + c.duration);
        return e;
    }
    // Find topmost clip at time t on V1/V2.
    bool clipAt(double t, const Clip **out, int *track) const {
        for (int ti = 1; ti >= 0; --ti) {
            const QVector<Clip> &tr = ti ? v2 : v1;
            for (const auto &c : tr) {
                if (t >= c.timelineStart && t < c.timelineStart + c.duration) {
                    if (out) *out = &c;
                    if (track) *track = ti;
                    return true;
                }
            }
        }
        return false;
    }
};

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget *parent = nullptr);

    void addClipToTrack(int track, const Clip &c, bool pushDown);
    void rippleDelete(const Clip &c);
    void splitAt(double t);

    // ---- selection (single clip; PropertiesPanel reads/writes this) ----
    // Pointer into the model — valid until the next mutation/sort. Use
    // selectedTrack()/selectedIndex() for stable identification instead.
    const Clip *selectedClip() const;
    int selectedTrack() const { return selTrack_; }
    int selectedIndex() const { return selIndex_; }
    bool hasSelection() const { return selTrack_ >= 0; }
    // Select topmost clip at time t (both tracks). Returns true if the
    // selection changed. Emits selectionChanged().
    bool selectAt(double t);
    void clearSelection();
    // Write effects onto the selected clip (pushes undo, emits modelChanged
    // + selectionChanged). Call with audioSync_ held (see MainWindow).
    bool setEffectsOnSelected(const ClipEffects &fx);

    // Undo/redo: automatic snapshots of the full model before each mutation.
    void pushUndo();
    bool undo();
    bool redo();
    bool canUndo() const { return !undoStack_.isEmpty(); }
    bool canRedo() const { return !redoStack_.isEmpty(); }

    void setPlayhead(double t) { playhead_ = t; followPlayhead(); update(); }
    double playhead() const { return playhead_; }
    double pxPerSec() const { return pxPerSec_; }
    void setZoomFactor(double pps) { pxPerSec_ = qBound(2.0, pps, 400.0); clampScroll(); update(); }
    // Resolve a source path to its playback path (proxy when fresh). Set by
    // MainWindow; strips decode through this so 4K sources read 960p proxies.
    void setPlaybackPathResolver(std::function<QString(const QString &)> r) { pathResolver_ = std::move(r); }
    TimelineModel model;

signals:
    void playheadMoved(double t);
    void modelChanged();
    void undoStateChanged();
    void selectionChanged();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    QSize sizeHint() const override { return {800, 260}; }

private:
    enum class Mode { None, Scrub, MoveClip, TrimStart, TrimEnd, Marble };
    Mode mode_ = Mode::None;

    struct ModelState {
        QVector<Clip> v1;
        QVector<Clip> v2;
        bool operator==(const ModelState &o) const { return v1 == o.v1 && v2 == o.v2; }
    };
    QVector<ModelState> undoStack_;
    QVector<ModelState> redoStack_;
    bool dragChanged_ = false; // current drag actually mutated the model
    static constexpr int kMaxUndo = 100;

    int trackAt(int y) const;              // -1 ruler, 0 V1, 1 V2, else -1
    double xToTime(int x) const;
    int timeToX(double t) const;
    int clipIndexAt(QVector<Clip> &track, double t, int *edge = nullptr);
    void pushDownClips(QVector<Clip> &track, double from, double gap);
    void pushRightClips(QVector<Clip> &track, double from, double gap);

    double playhead_ = 0.0;
    double pxPerSec_ = 40.0;
    const int snapPx_ = 8;
    double scrollX_ = 0.0;        // pixels of horizontal timeline scroll

    double timelineEnd() const;
    double fpsForRuler() const;
    double snapTime(double t, double ignoreStart, double ignoreEnd);
    void clampScroll();           // keep scroll within [0, contentWidth-view]
    void followPlayhead();        // scroll so playhead stays visible

    // interaction state
    double dragGrabOffset_ = 0.0; // grab point inside clip (seconds)
    int dragTrack_ = 0;
    int dragIndex_ = -1;

    // selection state: track 0/1 + index into that track's vector
    int selTrack_ = -1;
    int selIndex_ = -1;

    // ---- async filmstrip/waveform cache ----
    struct StripData {
        QVector<QImage> thumbs;   // fixed count per clip
        QVector<double> peaks;    // audio peaks 0..1
    };
    QHash<QString, StripData> stripCache_;  // key: path|in|dur
    QSet<QString> pendingStrips_;
    std::function<QString(const QString &)> pathResolver_; // source -> playback path
    static QString stripKey(const Clip &c) {
        return c.path + QLatin1Char('|') +
               QString::number(c.sourceIn, 'f', 3) + QLatin1Char('|') +
               QString::number(c.duration, 'f', 3);
    }
    void requestStrip(const Clip &c);    // kick worker if not cached/pending
    void paintStrip(QPainter &p, const Clip &c, const QRect &r);
};