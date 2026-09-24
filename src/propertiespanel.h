#pragma once

#include <QWidget>
#include <QPointer>

#include "clipeffects.h"

class QSlider;
class QDoubleSpinBox;
class QCheckBox;
class QLabel;
class TimelineWidget;
struct Clip;

// Right-dock panel: Effect Controls for the selected clip (Premiere's
// Effect Controls lite). Sliders/spinboxes for brightness/contrast/
// saturation/gamma with live preview invalidation and a Reset button.
// Reads/writes Clip::fx on the timeline's selected clip. When no clip is
// selected the panel shows a hint and disables the controls.
class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(TimelineWidget *timeline, QWidget *parent = nullptr);

    // Refresh widgets from the currently selected clip (call on selection
    // change or after undo/redo).
    void reload();

signals:
    // Emitted when the user edits a value; MainWindow invalidates preview.
    void effectsChanged();

private:
    void buildUi();
    void applyToClip();

    QPointer<TimelineWidget> timeline_;
    QDoubleSpinBox *bright_ = nullptr;
    QDoubleSpinBox *contr_ = nullptr;
    QDoubleSpinBox *sat_ = nullptr;
    QDoubleSpinBox *gam_ = nullptr;
    QCheckBox *bypass_ = nullptr;
    bool loading_ = false; // suppress signals during reload()
};