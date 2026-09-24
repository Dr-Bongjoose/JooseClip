#pragma once

#include <QtCore>
#include <QAudioSink>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QIODevice>
#include <atomic>
#include <functional>
#include <mutex>
#include "decoder.h"
#include "timelinewidget.h"

// Pull-based audio mixer: feeds QAudioSink from the decoders of clips
// playing on the timeline. Mixed to the output device layout.
class AudioEngine : public QObject {
    Q_OBJECT
public:
    using DecoderLookup = std::function<Decoder *(const QString &path)>;

    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    void setTimeline(const QVector<Clip> *v1, const QVector<Clip> *v2);
    void setDecoderLookup(DecoderLookup lookup) { lookup_ = std::move(lookup); }
    // Guards shared state (timeline vectors + decoder registry) that the
    // audio pull thread reads while the GUI thread edits it.
    void setSyncMutex(std::mutex *m) { syncMutex_ = m; }
    void playFrom(double t);
    void stop();
    bool playing() const { return playing_; }
    double playheadSeconds() const { return playhead_; }

private:
    qint64 writeData(char *data, qint64 len);

    // QIODevice fed to QAudioSink. Qt's ffmpeg backend only pulls when
    // bytesAvailable() reports data, so always claim availability.
    struct PullDevice : QIODevice {
        AudioEngine *eng = nullptr;
        qint64 readData(char *data, qint64 maxlen) override {
            return eng ? eng->writeData(data, maxlen) : 0;
        }
        qint64 writeData(const char *, qint64) override { return 0; }
        bool isSequential() const override { return true; }
        bool atEnd() const override { return false; }
        qint64 bytesAvailable() const override { return 1 << 20; }
    };

    bool ensureSink();
    void fill(int16_t *dst, int frames);

    QAudioSink *sink_ = nullptr;
    PullDevice *io_ = nullptr;
    const QVector<Clip> *v1_ = nullptr;
    const QVector<Clip> *v2_ = nullptr;
    DecoderLookup lookup_;
    std::mutex *syncMutex_ = nullptr;
    double playhead_ = 0.0;
    std::atomic<bool> playing_{false};
};