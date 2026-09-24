#pragma once

#include <QColor>
#include <QString>

// JooseClip theme — the JooseBooks palette (Dr-Bongjoose's design system).
// Single source of truth for every color the UI paints with. Panels draw
// these directly (no QSS theming of Qt internals beyond the app sheet).
namespace Theme {

// Core palette (from JooseBooks lib/main.dart + charts.dart)
inline QColor bg()      { return QColor(0x0D, 0x0D, 0x10); }  // kBg
inline QColor surface() { return QColor(0x17, 0x17, 0x1D); }  // kSurface
inline QColor surface2(){ return QColor(0x21, 0x21, 0x29); }  // kSurface2
inline QColor gold()    { return QColor(0xD4, 0xA8, 0x43); }  // kGold accent
inline QColor text()    { return QColor(0xEB, 0xEB, 0xF0); } // kText
inline QColor dim()    { return QColor(0x3A, 0x3A, 0x44); }  // kDimChart
inline QColor green()   { return QColor(0x4C, 0xD9, 0x64); } // kGreen (+)
inline QColor red()     { return QColor(0xE6, 0x59, 0x59); } // kRed (-)
// Derived: borders/hover drawn slightly lighter than surfaces.
inline QColor border() { return QColor(0x2A, 0x2A, 0x33); }
inline QColor hover()  { return QColor(0x2A, 0x2A, 0x38); }

// Full application stylesheet (QSS). Sets palette on widgets that do not
// paint themselves (menus, scrollbars, dialogs, sliders, buttons).
QString appStyleSheet();

// Colors for timeline painting (consumed by TimelineWidget).
inline QColor trackV1()  { return QColor(0x8a, 0x6d, 0x2f); } // dimmed gold
inline QColor trackV1Hover() { return QColor(0xB6, 0x92, 0x3C); }
inline QColor trackV2()  { return QColor(0x3A, 0x5F, 0x8A); }
inline QColor playhead(){ return gold(); }
inline QColor ruler()   { return surface(); }

} // namespace Theme