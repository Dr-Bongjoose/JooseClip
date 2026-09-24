#include "theme.h"

#include <QStringBuilder>

QString Theme::appStyleSheet() {
    // JooseBooks palette applied to Qt chrome. Self-painted widgets
    // (timeline, monitor bezel) use Theme:: colors directly.
    const QString bg      = QStringLiteral("#0D0D10");
    const QString surface = QStringLiteral("#17171D");
    const QString surf2   = QStringLiteral("#212129");
    const QString gold    = QStringLiteral("#D4A843");
    const QString text    = QStringLiteral("#EBEBF0");
    const QString dimc    = QStringLiteral("#3A3A44");
    const QString border  = QStringLiteral("#2A2A33");

    return QStringLiteral(R"(
* { outline: none; }
QMainWindow, QDialog { background: %1; }
QMenuBar { background: %1; color: %5; }
QMenuBar::item { padding: 4px 10px; border-radius: 3px; }
QMenuBar::item:selected { background: %3; }
QMenu { background: %2; color: %5; border: 1px solid %8; padding: 4px; }
QMenu::item { padding: 5px 24px 5px 12px; border-radius: 3px; }
QMenu::item:selected { background: %3; }
QMenu::separator { height: 1px; background: %8; margin: 4px 8px; }
QDockWidget { color: %5; }
QDockWidget::title { background: %2; border: 1px solid %8; padding: 6px; }
QLabel { color: %5; }
QStatusBar { background: %1; color: %7; }
QToolTip { background: %3; color: %5; border: 1px solid %8; }
QListWidget { background: %2; color: %5; border: 1px solid %8; }
QListWidget::item { padding: 6px; border-radius: 3px; }
QListWidget::item:selected { background: %3; color: #141210; }
QListWidget::item:hover { background: %9; }
QScrollBar:vertical { background: %1; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: %7; border-radius: 5px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: %4; }
QScrollBar:horizontal { background: %1; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background: %7; border-radius: 5px; min-width: 24px; }
QScrollBar::handle:horizontal:hover { background: %4; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QPushButton { background: %3; color: #141210; border: none; padding: 6px 14px; border-radius: 3px; font-weight: 600; }
QPushButton:hover { background: %4; }
QPushButton:pressed { background: %4; }
QPushButton:disabled { background: %7; color: %5; }
QSlider::groove:horizontal { height: 4px; background: %7; border-radius: 2px; }
QSlider::handle:horizontal { width: 12px; height: 12px; background: %4; border-radius: 6px; margin: -5px 0; }
QDoubleSpinBox, QSpinBox, QComboBox, QLineEdit { background: %2; color: %5; border: 1px solid %8; border-radius: 3px; padding: 4px; }
QComboBox:focus, QDoubleSpinBox:focus, QSpinBox:focus, QLineEdit:focus { border: 1px solid %4; }
QGroupBox { border: 1px solid %8; border-radius: 4px; margin-top: 10px; color: %4; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
QProgressBar { background: %2; border: 1px solid %8; border-radius: 3px; color: %5; text-align: center; }
QProgressBar::chunk { background: %4; border-radius: 2px; }
QCheckBox { color: %5; }
QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid %8; border-radius: 3px; background: %2; }
QCheckBox::indicator:checked { background: %4; border-color: %4; }
)")
        .arg(bg, surface, surf2, gold, text, dimc, dimc, border, QStringLiteral("#2A2A38"));
}