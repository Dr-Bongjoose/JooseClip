#include "clipeffects.h"
#include <QtMath>
#include <QtGlobal>

void ClipEffects::clamp() {
    brightness = qBound(-1.0, brightness, 1.0);
    contrast   = qBound(0.1, contrast, 3.0);
    saturation = qBound(0.0, saturation, 3.0);
    gamma      = qBound(0.1, gamma, 4.0);
}

void applyEffects(QImage &img, const ClipEffects &fx) {
    if (img.isNull() || fx.isIdentity()) return;

    // Build a 256-entry lookup for brightness/contrast/gamma (per channel
    // value ops). Saturation needs per-pixel math.
    quint8 lut[256];
    const double b = fx.brightness * 255.0;
    for (int i = 0; i < 256; ++i) {
        double v = i;
        v += b;                                   // brightness
        v = (v - 128.0) * fx.contrast + 128.0;    // contrast (pivot 128)
        v = qBound(0.0, v, 255.0);
        v = 255.0 * qPow(v / 255.0, 1.0 / fx.gamma); // gamma
        lut[i] = quint8(qBound(0.0, double(qRound(v)), 255.0));
    }

    QImage out = (img.format() == QImage::Format_RGB888)
                     ? img
                     : img.convertToFormat(QImage::Format_RGB888);
    const int w = out.width(), h = out.height();
    const bool satOnly = false;
    Q_UNUSED(satOnly);

    for (int y = 0; y < h; ++y) {
        uchar *line = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            uchar *px = line + x * 3;
            int r = lut[px[0]], g = lut[px[1]], b = lut[px[2]];
            if (fx.saturation != 1.0) {
                // luminance-preserving saturation
                const double lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                r = int(qRound(lum + (r - lum) * fx.saturation));
                g = int(qRound(lum + (g - lum) * fx.saturation));
                b = int(qRound(lum + (b - lum) * fx.saturation));
                r = qBound(0, r, 255); g = qBound(0, g, 255); b = qBound(0, b, 255);
            }
            px[0] = uchar(r); px[1] = uchar(g); px[2] = uchar(b);
        }
    }
    if (out.bits() != img.bits()) img = out; // cheap when no conversion happened
}