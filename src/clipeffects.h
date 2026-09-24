#pragma once

#include <QImage>

// Per-clip color effects (v0.2). Default-constructed = identity (no change).
// Stored inside Clip; applied to decoded frames before display/export.
struct ClipEffects {
    double brightness = 0.0;   // -1.0 .. 1.0  (0 = none)
    double contrast   = 1.0;   //  0.1 .. 3.0  (1 = none)
    double saturation = 1.0;   //  0.0 .. 3.0  (1 = none)
    double gamma      = 1.0;   //  0.1 .. 4.0  (1 = none)

    bool operator==(const ClipEffects &o) const {
        return brightness == o.brightness && contrast == o.contrast &&
               saturation == o.saturation && gamma == o.gamma;
    }
    bool isIdentity() const {
        return brightness == 0.0 && contrast == 1.0 &&
               saturation == 1.0 && gamma == 1.0;
    }
    // Clamp all fields into their valid ranges.
    void clamp();
};

// Apply effects to img in place. Identity effects must be a fast no-op.
// Works on any QImage format; converts to RGB888 internally only when
// a parameter actually needs changing.
void applyEffects(QImage &img, const ClipEffects &fx);