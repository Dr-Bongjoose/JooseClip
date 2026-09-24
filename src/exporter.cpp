// TEMPORARY STUB — replaced by the real implementation on branch
// agent/exporter (merge pending). Exists so exportdialog.cpp links while
// the exporter module lands. Do not extend.
#include "exporter.h"

Exporter::Exporter(QObject *parent) : QObject(parent) {}

qint64 Exporter::totalFrames(const TimelineModel &, double) { return 0; }

bool Exporter::run(const TimelineModel &, const Settings &, double, double) {
    emit finished(false, QStringLiteral("Exporter module not yet merged."));
    return false;
}