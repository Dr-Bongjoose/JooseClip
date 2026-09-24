#include "audioengine.h"

#include <QAudioFormat>
#include <cstring>

AudioEngine::AudioEngine(QObject *parent) : QObject(parent) {}

AudioEngine::~AudioEngine() { stop(); }

void AudioEngine::setTimeline(const QVector<Clip> *v1, const QVector<Clip> *v2) {
    v1_ = v1;
    v2_ = v2;
}

bool AudioEngine::ensureSink() {
    if (sink_) return true;
    QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) return false;
    QAudioFormat fmt;
    fmt.setSampleRate(48000);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);
    if (!dev.isFormatSupported(fmt)) fmt = dev.preferredFormat();
    sink_ = new QAudioSink(dev, fmt, this);
    io_ = new PullDevice;
    io_->eng = this;
    io_->open(QIODevice::ReadOnly);
    return true;
}

void AudioEngine::playFrom(double t) {
    if (!ensureSink()) return;
    playhead_ = t;
    if (sink_->state() == QAudio::SuspendedState) {
        sink_->resume();
    } else {
        sink_->start(io_);
    }
    playing_ = true;
}

void AudioEngine::stop() {
    playing_ = false;
    if (sink_ && sink_->state() == QAudio::ActiveState) sink_->suspend();
}

// Mix one buffer: find clips covering [playhead_, playhead_ + dur), decode
// s16 PCM from each via the decoder, mix (saturating), advance playhead by
// the delivered duration. Runs on the QAudioSink pull thread.
void AudioEngine::fill(int16_t *dst, int frames) {
    const int outRate = sink_->format().sampleRate();
    const int outCh = sink_->format().channelCount();
    std::memset(dst, 0, size_t(frames) * outCh * sizeof(int16_t));
    if (!playing_ || !lookup_) return;

    const double t0 = playhead_;
    const double t1 = t0 + double(frames) / outRate;

    // The timeline vectors and decoder registry are GUI-thread state.
    // Hold the sync mutex for the whole pull (decoding is the bulk of the
    // work, but contention is bounded: GUI only locks during edits/renders
    // that touch the same objects).
    std::unique_lock<std::mutex> lock;
    if (syncMutex_) lock = std::unique_lock<std::mutex>(*syncMutex_);

    static thread_local std::vector<int16_t> pcm;
    std::vector<const Clip *> active; // clips covering the buffer
    for (const QVector<Clip> *tr : {v1_, v2_}) {
        if (!tr) continue;
        for (const Clip &c : *tr) {
            if (c.timelineStart >= t1) continue;               // starts after buffer ends
            if (c.timelineStart + c.duration <= t0) continue;  // ends before buffer starts
            active.push_back(&c);
        }
    }

    for (const Clip *cp : active) {
        const Clip &c = *cp;
        // Clip may start/end mid-buffer; decodeAudio handles clamping via
        // [from, from+maxSec) with from possibly beyond file duration.
        double local = qMax(0.0, t0 - c.timelineStart + c.sourceIn);
        double maxSec = double(frames) / outRate;

        Decoder *dec = lookup_(c.path);
        if (!dec || !dec->hasAudio()) continue;

        pcm.clear();
        int64_t got = dec->decodeAudio(local, maxSec, pcm);
        if (got <= 0) continue;

        // mix: nearest-sample resample if device rate differs
        const int inCh = dec->channels();
        const double srcPerOut = double(dec->sampleRate()) / outRate;
        for (int f = 0; f < frames; ++f) {
            int64_t srcFrame = int64_t(f * srcPerOut);
            if (srcFrame >= got) break;
            for (int ch = 0; ch < outCh; ++ch) {
                int inChIdx = ch < inCh ? ch : inCh - 1; // up/downmix: dup/mono
                int sample = pcm[size_t(srcFrame) * inCh + inChIdx];
                int32_t acc = dst[size_t(f) * outCh + ch] + sample / 2;
                if (acc > 32767) acc = 32767;
                if (acc < -32768) acc = -32768;
                dst[size_t(f) * outCh + ch] = int16_t(acc);
            }
        }
    }
    playhead_ = t1;
}

qint64 AudioEngine::writeData(char *data, qint64 len) {
    if (!playing_ || !sink_) return 0;
    const int frameBytes = sink_->format().channelCount() * int(sizeof(int16_t));
    int frames = int(len / frameBytes);
    if (frames <= 0) return 0;
    fill(reinterpret_cast<int16_t *>(data), frames);
    // returning less than len makes QAudioSink think we're starving; hand
    // back the whole buffer (fill() zero-pads when no clip covers playhead)
    return len;
}