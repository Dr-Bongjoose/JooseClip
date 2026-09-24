#include "propertiespanel.h"

#include "timelinewidget.h"
#include "theme.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVariant>
#include <QVBoxLayout>

namespace {
// The header contract has no members for the Reset button or the status
// label, so they are created in buildUi() and reached back via findChild()
// with stable object names.
const char kResetName[]  = "fxReset";
const char kStatusName[] = "fxStatusLabel";
// Pre-bypass values (QVariantList of 4 doubles: bright, contrast, sat, gamma).
// The header contract has no stash member, so the stash lives as a dynamic
// property on the panel instead.
const char kStashProp[] = "fxBypassStash";

const char kHint[] = "Select a clip on the timeline to edit its effects.";
} // namespace

PropertiesPanel::PropertiesPanel(TimelineWidget *timeline, QWidget *parent)
    : QWidget(parent), timeline_(timeline) {
    buildUi();
    if (timeline_) {
        connect(timeline_, &TimelineWidget::selectionChanged, this, &PropertiesPanel::reload);
        connect(timeline_, &TimelineWidget::modelChanged, this, &PropertiesPanel::reload);
        connect(timeline_, &TimelineWidget::undoStateChanged, this, &PropertiesPanel::reload);
    }
    reload();
}

void PropertiesPanel::buildUi() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    auto *group = new QGroupBox(tr("Effect Controls"), this);
    auto *grid = new QGridLayout(group);
    grid->setContentsMargins(10, 20, 10, 10);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(1, 1);

    // Spinbox factory: fixed ranges/step/decimals per ClipEffects contract.
    auto mkSpin = [this](const char *name, double lo, double hi, double def) {
        auto *s = new QDoubleSpinBox(this);
        s->setObjectName(QString::fromLatin1(name));
        s->setRange(lo, hi);
        s->setSingleStep(0.01);
        s->setDecimals(2);
        s->setValue(def);
        connect(s, &QDoubleSpinBox::valueChanged, this, [this](double) {
            if (!loading_) applyToClip();
        });
        return s;
    };

    grid->addWidget(new QLabel(tr("Brightness"), group), 0, 0);
    bright_ = mkSpin("fxBrightness", -1.0, 1.0, 0.0);
    grid->addWidget(bright_, 0, 1);

    grid->addWidget(new QLabel(tr("Contrast"), group), 1, 0);
    contr_ = mkSpin("fxContrast", 0.1, 3.0, 1.0);
    grid->addWidget(contr_, 1, 1);

    grid->addWidget(new QLabel(tr("Saturation"), group), 2, 0);
    sat_ = mkSpin("fxSaturation", 0.0, 3.0, 1.0);
    grid->addWidget(sat_, 2, 1);

    grid->addWidget(new QLabel(tr("Gamma"), group), 3, 0);
    gam_ = mkSpin("fxGamma", 0.1, 4.0, 1.0);
    grid->addWidget(gam_, 3, 1);

    bypass_ = new QCheckBox(tr("Bypass"), group);
    bypass_->setObjectName("fxBypass");
    grid->addWidget(bypass_, 4, 0);

    auto *reset = new QPushButton(tr("Reset"), group);
    reset->setObjectName(QLatin1String(kResetName));
    // Gold Reset button (matches the app QSS button idiom: 6px/14px padding,
    // 3px radius, bold). Drawn from Theme:: so it works with or without the
    // global stylesheet (e.g. in standalone tests).
    const QColor gold  = Theme::gold();
    const QColor hover = gold.lighter(120);
    reset->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: #141210; border: none;"
        " padding: 6px 14px; border-radius: 3px; font-weight: 600; }"
        "QPushButton:hover { background: %2; }"
        "QPushButton:pressed { background: %2; }"
        "QPushButton:disabled { background: %3; color: %4; }")
        .arg(gold.name(), hover.name(), Theme::dim().name(), Theme::text().name()));
    grid->addWidget(reset, 4, 1, 1, 1, Qt::AlignRight);

    auto *status = new QLabel(group);
    status->setObjectName(QLatin1String(kStatusName));
    status->setWordWrap(true);
    grid->addWidget(status, 5, 0, 1, 2);

    root->addWidget(group);
    root->addStretch(1);

    // Bypass: stash current values and apply identity while checked; restore
    // the stash when unchecked. reload() (re-triggered by modelChanged, or
    // called explicitly below in case the apply was a no-op) flips the
    // spinbox enable state based on the checkbox.
    connect(bypass_, &QCheckBox::toggled, this, [this](bool on) {
        if (loading_ || !timeline_ || !timeline_->hasSelection()) return;
        loading_ = true;
        if (on) {
            setProperty(kStashProp, QVariantList{bright_->value(), contr_->value(),
                                                 sat_->value(), gam_->value()});
            bright_->setValue(0.0);
            contr_->setValue(1.0);
            sat_->setValue(1.0);
            gam_->setValue(1.0);
        } else {
            const QVariantList stash = property(kStashProp).toList();
            if (stash.size() == 4) {
                bright_->setValue(stash.at(0).toDouble());
                contr_->setValue(stash.at(1).toDouble());
                sat_->setValue(stash.at(2).toDouble());
                gam_->setValue(stash.at(3).toDouble());
            }
        }
        loading_ = false;
        applyToClip(); // same path as a manual spinbox edit
        reload();      // sync enable/disable even if the apply was a no-op
    });

    // Reset: identity (0, 1, 1, 1) through the same path as a manual edit.
    connect(reset, &QPushButton::clicked, this, [this]() {
        if (loading_ || !timeline_ || !timeline_->hasSelection()) return;
        loading_ = true;
        bright_->setValue(0.0);
        contr_->setValue(1.0);
        sat_->setValue(1.0);
        gam_->setValue(1.0);
        loading_ = false;
        applyToClip();
        reload();
    });
}

void PropertiesPanel::reload() {
    // Suppress valueChanged -> applyToClip() while refreshing from the model
    // (setEffectsOnSelected emits modelChanged, which re-enters reload()).
    loading_ = true;

    const Clip *c = timeline_ ? timeline_->selectedClip() : nullptr;
    const bool has = timeline_ && timeline_->hasSelection() && c != nullptr;

    if (has) {
        bright_->setValue(c->fx.brightness);
        contr_->setValue(c->fx.contrast);
        sat_->setValue(c->fx.saturation);
        gam_->setValue(c->fx.gamma);
    } else {
        bright_->setValue(0.0);
        contr_->setValue(1.0);
        sat_->setValue(1.0);
        gam_->setValue(1.0);
        // Guarded by loading_: the toggled handler skips, no side effects.
        bypass_->setChecked(false);
    }

    if (auto *status = findChild<QLabel*>(QLatin1String(kStatusName))) {
        status->setText(has ? QFileInfo(c->path).fileName()
                            : tr(kHint));
    }

    const bool spin = has && !bypass_->isChecked();
    bright_->setEnabled(spin);
    contr_->setEnabled(spin);
    sat_->setEnabled(spin);
    gam_->setEnabled(spin);
    bypass_->setEnabled(has);
    if (auto *reset = findChild<QPushButton*>(QLatin1String(kResetName)))
        reset->setEnabled(has);

    loading_ = false;
}

void PropertiesPanel::applyToClip() {
    if (!timeline_ || loading_ || !timeline_->hasSelection()) return;
    ClipEffects fx;
    fx.brightness = bright_->value();
    fx.contrast   = contr_->value();
    fx.saturation = sat_->value();
    fx.gamma      = gam_->value();
    fx.clamp();
    if (!timeline_->setEffectsOnSelected(fx)) return; // no selection / no change
    emit effectsChanged(); // MainWindow re-renders the preview
}