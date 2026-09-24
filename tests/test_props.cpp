// Standalone test for the PropertiesPanel Effect Controls: bare
// TimelineWidget (no MainWindow), panel wiring, spinbox -> Clip::fx apply,
// undo revert, Reset, and Bypass stash/restore.
//
// Build via tests/test_props.pro in a scratch build dir (NOT part of the
// main build), run with QT_QPA_PLATFORM=offscreen.
#include "propertiespanel.h"
#include "timelinewidget.h"
#include "clipeffects.h"

#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <cstdio>

static int passed = 0, failed = 0;
static void check(bool ok, const char *what) {
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) ++passed; else ++failed;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    TimelineWidget timeline;
    PropertiesPanel panel(&timeline);
    panel.show(); // offscreen platform: no real window appears

    int fxChanged = 0;
    QObject::connect(&panel, &PropertiesPanel::effectsChanged,
                     [&fxChanged]() { ++fxChanged; });

    auto *bright = panel.findChild<QDoubleSpinBox *>("fxBrightness");
    auto *contr  = panel.findChild<QDoubleSpinBox *>("fxContrast");
    auto *bypass = panel.findChild<QCheckBox *>("fxBypass");
    auto *reset  = panel.findChild<QPushButton *>("fxReset");
    auto *status = panel.findChild<QLabel *>("fxStatusLabel");
    if (!bright || !contr || !bypass || !reset || !status) {
        printf("FATAL: Effect Controls widgets not found (bright=%p contr=%p "
               "bypass=%p reset=%p status=%p)\n",
               (void *)bright, (void *)contr, (void *)bypass,
               (void *)reset, (void *)status);
        return 2;
    }

    // --- no selection: controls disabled, hint shown ------------------------
    check(!timeline.hasSelection(), "no selection initially");
    check(!bright->isEnabled(), "spinbox disabled without selection");
    check(!bypass->isEnabled(), "bypass disabled without selection");
    check(!reset->isEnabled(), "reset disabled without selection");
    check(!status->text().isEmpty(), "status label shows hint (no selection)");

    // --- dummy clip; nonexistent path is fine (addClipToTrack does not
    //     probe; only fx/undo logic matters here) ----------------------------
    Clip clip;
    clip.path = QStringLiteral("/nonexistent/dummy_clip.mp4");
    clip.sourceIn = 0.0;
    clip.timelineStart = 0.0;
    clip.duration = 10.0;
    timeline.addClipToTrack(0, clip, false);

    check(timeline.selectAt(5.0), "selectAt(5.0) selects the clip");
    check(timeline.hasSelection() && timeline.selectedClip() != nullptr,
          "hasSelection() + selectedClip() valid");
    check(bright->isEnabled(), "spinbox enabled with selection");
    check(status->text().contains(QLatin1String("dummy_clip.mp4")),
          "status label shows clip filename");
    check(bright->value() == 0.0 && contr->value() == 1.0,
          "spinboxes loaded from clip fx");

    // --- spinbox edit applies fx to the selected clip ----------------------
    bright->setValue(0.5);
    check(timeline.selectedClip()->fx.brightness == 0.5,
          "fx.brightness == 0.5 after spinbox edit");
    check(fxChanged == 1, "effectsChanged emitted once after edit");

    // --- undo reverts -------------------------------------------------------
    // NOTE: TimelineWidget::undo() clears the selection (indexes are stale
    // after a model swap), so re-select before reading fx back.
    check(timeline.undo(), "undo() returns true");
    check(!timeline.hasSelection(), "undo cleared selection (timeline behavior)");
    check(timeline.selectAt(5.0), "re-selected clip after undo");
    check(timeline.selectedClip()->fx.brightness == 0.0,
          "fx.brightness reverted to 0.0 after undo");

    // --- Reset: identity (0, 1, 1, 1) via the same path ----------------------
    bright->setValue(0.5);
    contr->setValue(2.0);
    check(timeline.selectedClip()->fx.brightness == 0.5 &&
          timeline.selectedClip()->fx.contrast == 2.0,
          "edits applied (brightness 0.5, contrast 2.0)");
    reset->click();
    check(timeline.selectedClip()->fx.isIdentity(),
          "Reset applies identity (0, 1, 1, 1)");
    check(bright->value() == 0.0 && contr->value() == 1.0,
          "spinboxes synced after Reset");

    // --- Bypass: stash + identity while checked, restore on uncheck ----------
    bright->setValue(0.5);
    check(timeline.selectedClip()->fx.brightness == 0.5,
          "brightness 0.5 before bypass");
    bypass->setChecked(true);
    check(timeline.selectedClip()->fx.isIdentity(), "bypass applies identity effects");
    check(!bright->isEnabled(), "spinboxes disabled while bypassed");
    bypass->setChecked(false);
    check(bright->isEnabled(), "spinboxes re-enabled after bypass");
    check(timeline.selectedClip()->fx.brightness == 0.5,
          "bypass restores stashed values");

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}