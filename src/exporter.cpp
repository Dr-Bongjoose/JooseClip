// Exporter: offline timeline render -> H.264/AAC MP4 via the libav C API.
//
// Walks the output range [rangeStart, rangeEnd) frame by frame in output
// time. For each output frame t the topmost clip is found through
// TimelineModel::clipAt (V2 overlays V1); its source frame is decoded at
//     sourceIn + (t - timelineStart)
// via a per-source Decoder, optionally passed through ClipEffects, then
// scaled (aspect preserved) and letterboxed with black bars onto the output
// canvas, converted to yuv420p and encoded as H.264.
//
// Audio: every clip covering the output time (from BOTH tracks) contributes;
// PCM is pulled from the same Decoders as interleaved s16 at the source rate,
// resampled per clip to 48 kHz stereo s16 via SwrContext, mixed by average
// of contributing clips, and encoded as AAC (converted to the encoder's
// required sample format with one final SwrContext).
//
// Conventions honored (they caused real bugs before):
//   * every av_* return code is checked,
//   * every libav object is freed exactly once (EncState RAII below),
//   * thread_count = 1 on both encoder contexts (VM constraint),
//   * Decoder may return any QImage format -> normalize to RGB888,
//   * no C++ exceptions,
//   * run() keeps all state local -> safe from a worker thread; cancel()
//     is honored via the atomic cancelled_ and still yields a playable
//     partial file (encoders flushed, trailer written).

#include "exporter.h"
#include "clipeffects.h"

#include <QHash>
#include <QImage>
#include <QVector>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace {

constexpr int kOutSampleRate = 48000;
constexpr int kOutChannels = 2;

QString avErrStr(int e)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(e, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

// Everything libav one export run allocates, freed exactly once on every
// exit path (single destructor; no exceptions anywhere in this file).
struct EncState {
    AVFormatContext *oc = nullptr;
    AVCodecContext *vc = nullptr;      // H.264
    AVCodecContext *ac = nullptr;      // AAC
    AVStream *vs = nullptr;
    AVStream *as = nullptr;
    AVFrame *vframe = nullptr;         // yuv420p canvas frame
    AVFrame *aframe = nullptr;         // AAC frame
    AVPacket *pkt = nullptr;
    SwsContext *rgb2yuv = nullptr;          // canvas RGB -> yuv420p
    SwrContext *mixSwr = nullptr;           // mixed s16 -> encoder sample fmt
    QHash<QString, SwsContext *> scaleCtxs; // per source size: RGB -> fitted RGB
    QHash<QString, Decoder *> decoders;      // one Decoder per source path
    QVector<SwrContext *> clipSwrs;         // per-clip s16@src -> s16@48k stereo
    bool headerWritten = false;
    bool ioOpened = false;

    ~EncState() {
        for (SwsContext *s : scaleCtxs) if (s) sws_freeContext(s);
        if (rgb2yuv) sws_freeContext(rgb2yuv);
        for (SwrContext *s : clipSwrs) { SwrContext *ps = s; swr_free(&ps); }
        if (mixSwr) swr_free(&mixSwr);
        if (pkt) av_packet_free(&pkt);
        if (vframe) av_frame_free(&vframe);
        if (aframe) av_frame_free(&aframe);
        if (vc) avcodec_free_context(&vc);
        if (ac) avcodec_free_context(&ac);
        for (Decoder *d : decoders) delete d;
        if (oc) {
            if (ioOpened && oc->pb) avio_closep(&oc->pb);
            avformat_free_context(oc);
        }
    }
};

// Feed one frame (nullptr = flush) to an encoder and write out every packet
// it emits. EAGAIN only means "buffered, keep feeding". Returns false with
// *err set on hard failure.
bool encodeFrame(EncState &st, AVCodecContext *ctx, AVStream *stream,
                 AVFrame *frame, QString *err)
{
    const int re = avcodec_send_frame(ctx, frame);
    if (re < 0) {
        *err = QStringLiteral("avcodec_send_frame: %1").arg(avErrStr(re));
        return false;
    }
    for (;;) {
        const int r = avcodec_receive_packet(ctx, st.pkt);
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
            return true;
        if (r < 0) {
            *err = QStringLiteral("avcodec_receive_packet: %1").arg(avErrStr(r));
            return false;
        }
        av_packet_rescale_ts(st.pkt, ctx->time_base, stream->time_base);
        st.pkt->stream_index = stream->index;
        const int w = av_interleaved_write_frame(st.oc, st.pkt);
        if (w < 0) {
            *err = QStringLiteral("av_interleaved_write_frame: %1").arg(avErrStr(w));
            return false;
        }
    }
}

} // namespace

Exporter::Exporter(QObject *parent) : QObject(parent) {}

qint64 Exporter::totalFrames(const TimelineModel &model, double fps)
{
    if (fps <= 0.0 || model.sequenceEnd() <= 0.0)
        return 0;
    return qint64(std::ceil(model.sequenceEnd() * fps - 1e-9));
}

bool Exporter::run(const TimelineModel &model, const Settings &settings,
                   double rangeStart, double rangeEnd)
{
    // Each run() is a fresh job: a cancel() aimed at a previous run must
    // not bleed into this one.
    cancelled_ = false;

    // ---- validate & normalize --------------------------------------------
    if (settings.outPath.isEmpty()) {
        emit finished(false, QStringLiteral("No output path"));
        return false;
    }
    if (settings.width < 2 || settings.height < 2 ||
        settings.width > 8192 || settings.height > 8192) {
        emit finished(false, QStringLiteral("Invalid output size %1x%2")
                                 .arg(settings.width).arg(settings.height));
        return false;
    }
    if (settings.fps < 1 || settings.fps > 240) {
        emit finished(false, QStringLiteral("Invalid frame rate %1").arg(settings.fps));
        return false;
    }
    const double end = rangeEnd < 0.0 ? model.sequenceEnd() : rangeEnd;
    if (end <= rangeStart + 1e-9) {
        emit finished(false, QStringLiteral("Empty export range"));
        return false;
    }
    const double total = end - rangeStart;
    const double frameDt = 1.0 / double(settings.fps);
    const qint64 N = qint64(std::ceil(total * double(settings.fps) - 1e-9));
    const int W = settings.width & ~1;   // yuv420p chroma needs even sizes
    const int H = settings.height & ~1;

    EncState st;

    // ---- snapshot the clips we need (copies; never hold refs into the
    //      model's QVectors across decode calls) ---------------------------
    struct LocalClip {
        QString path;
        MediaInfo info;
        double sourceIn = 0.0;
        double timelineStart = 0.0;
        double duration = 0.0;
        ClipEffects fx;
        Decoder *dec = nullptr;
        bool audio = false;
        int srcRate = 0;
        int srcCh = 0;
        SwrContext *swr = nullptr;   // per-clip resampler (stateful)
    };
    std::vector<LocalClip> clips;
    for (int track = 0; track < 2; ++track) {
        const QVector<Clip> &tr = track ? model.v2 : model.v1;
        for (const Clip &c : tr) {
            if (c.duration <= 0.0)
                continue;
            if (c.timelineStart >= end || c.timelineStart + c.duration <= rangeStart)
                continue;
            LocalClip lc;
            lc.path = c.path;
            lc.info = c.info;
            lc.sourceIn = c.sourceIn;
            lc.timelineStart = c.timelineStart;
            lc.duration = c.duration;
            lc.fx = c.fx;
            clips.push_back(lc);
        }
    }
    if (clips.empty()) {
        emit finished(false, QStringLiteral("No clips within the export range"));
        return false;
    }

    // ---- one Decoder per unique source path -------------------------------
    for (const LocalClip &lc : clips) {
        if (st.decoders.contains(lc.path))
            continue;
        MediaInfo mi = lc.info;
        mi.path = lc.path;            // Clip::path is authoritative
        st.decoders.insert(lc.path, new Decoder(mi));
    }

    // ---- audio availability (opens each decoder's audio side) ------------
    bool anyAudio = false;
    for (Decoder *d : st.decoders) {
        std::vector<int16_t> probe;
        if (d->decodeAudio(0.0, 1e-6, probe) >= 0)
            anyAudio = true;          // 0 samples returned still means "open ok"
    }
    for (LocalClip &lc : clips) {
        Decoder *d = st.decoders.value(lc.path, nullptr);
        lc.dec = d;
        lc.audio = d && d->hasAudio();
        lc.srcRate = d ? d->sampleRate() : 0;
        lc.srcCh = d ? d->channels() : 0;
        if (!lc.audio || lc.srcRate <= 0 || lc.srcCh <= 0) {
            lc.audio = false;
            continue;
        }
        AVChannelLayout inCh, outCh;
        av_channel_layout_default(&inCh, lc.srcCh);
        av_channel_layout_default(&outCh, kOutChannels);
        SwrContext *swr = nullptr;
        const int re = swr_alloc_set_opts2(&swr,
                                           &outCh, AV_SAMPLE_FMT_S16, kOutSampleRate,
                                           &inCh, AV_SAMPLE_FMT_S16, lc.srcRate,
                                           0, nullptr);
        if (re >= 0 && swr && swr_init(swr) >= 0) {
            lc.swr = swr;
            st.clipSwrs.append(swr);
        } else {
            if (swr) swr_free(&swr);
            lc.audio = false;         // cannot resample -> drop this clip's audio
        }
    }

    // ---- output context ----------------------------------------------------
    int re = avformat_alloc_output_context2(&st.oc, nullptr, nullptr,
                                            settings.outPath.toUtf8().constData());
    if (re < 0 || !st.oc) {
        re = avformat_alloc_output_context2(&st.oc, nullptr, "mp4",
                                            settings.outPath.toUtf8().constData());
        if (re < 0 || !st.oc) {
            emit finished(false, QStringLiteral("avformat_alloc_output_context2: %1")
                                     .arg(avErrStr(re)));
            return false;
        }
    }

    // ---- H.264 encoder -----------------------------------------------------
    const AVCodec *vcodec = avcodec_find_encoder_by_name("libx264");
    if (!vcodec)
        vcodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!vcodec) {
        emit finished(false, QStringLiteral("No H.264 encoder in this FFmpeg build"));
        return false;
    }
    st.vs = avformat_new_stream(st.oc, nullptr);
    if (!st.vs) {
        emit finished(false, QStringLiteral("avformat_new_stream failed"));
        return false;
    }
    st.vs->time_base = av_make_q(1, settings.fps);
    st.vs->avg_frame_rate = av_make_q(settings.fps, 1);
    st.vc = avcodec_alloc_context3(vcodec);
    if (!st.vc) {
        emit finished(false, QStringLiteral("avcodec_alloc_context3 failed"));
        return false;
    }
    st.vc->width = W;
    st.vc->height = H;
    st.vc->pix_fmt = AV_PIX_FMT_YUV420P;
    st.vc->time_base = av_make_q(1, settings.fps);
    st.vc->framerate = av_make_q(settings.fps, 1);
    st.vc->bit_rate = qint64(settings.videoKbps) * 1000;
    st.vc->gop_size = std::max(12, settings.fps * 2);
    st.vc->max_b_frames = 0;          // simplest, fully deterministic pts/dts
    st.vc->thread_count = 1;          // VM constraint (see decoder.cpp note)
    if (st.oc->oformat->flags & AVFMT_GLOBALHEADER)
        st.vc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;   // required for mp4
    re = avcodec_open2(st.vc, vcodec, nullptr);
    if (re < 0) {
        emit finished(false, QStringLiteral("avcodec_open2(h264): %1").arg(avErrStr(re)));
        return false;
    }
    re = avcodec_parameters_from_context(st.vs->codecpar, st.vc);
    if (re < 0) {
        emit finished(false, QStringLiteral("avcodec_parameters_from_context(h264): %1")
                                     .arg(avErrStr(re)));
        return false;
    }

    // ---- AAC encoder (only if some source actually has audio) -------------
    AVSampleFormat aFmt = AV_SAMPLE_FMT_S16;
    if (anyAudio) {
        const AVCodec *acodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!acodec) {
            emit finished(false, QStringLiteral("No AAC encoder in this FFmpeg build"));
            return false;
        }
        st.as = avformat_new_stream(st.oc, nullptr);
        if (!st.as) {
            emit finished(false, QStringLiteral("avformat_new_stream failed"));
            return false;
        }
        // The encoder dictates the sample format (native AAC wants fltp);
        // the mix stage stays s16 and mixSwr converts at the end.
        aFmt = AV_SAMPLE_FMT_NONE;
        const AVSampleFormat *fmts = nullptr;
        int nfmt = 0;
        if (avcodec_get_supported_config(nullptr, acodec, AV_CODEC_CONFIG_SAMPLE_FORMAT,
                                         0, reinterpret_cast<const void **>(&fmts),
                                         &nfmt) >= 0 && nfmt > 0) {
            for (int i = 0; i < nfmt; ++i) {
                const AVSampleFormat f = fmts[i];
                if (f == AV_SAMPLE_FMT_S16) { aFmt = f; break; }
                if (aFmt == AV_SAMPLE_FMT_NONE && f == AV_SAMPLE_FMT_FLTP)
                    aFmt = f;
            }
            if (aFmt == AV_SAMPLE_FMT_NONE)
                aFmt = fmts[0];
        }
        if (aFmt == AV_SAMPLE_FMT_NONE)
            aFmt = AV_SAMPLE_FMT_FLTP;   // sane default for AAC

        st.ac = avcodec_alloc_context3(acodec);
        if (!st.ac) {
            emit finished(false, QStringLiteral("avcodec_alloc_context3 failed"));
            return false;
        }
        AVChannelLayout stereo;
        av_channel_layout_default(&stereo, kOutChannels);
        re = av_channel_layout_copy(&st.ac->ch_layout, &stereo);
        if (re < 0) {
            emit finished(false, QStringLiteral("av_channel_layout_copy: %1").arg(avErrStr(re)));
            return false;
        }
        st.ac->sample_rate = kOutSampleRate;
        st.ac->sample_fmt = aFmt;
        st.ac->bit_rate = qint64(settings.audioKbps) * 1000;
        st.ac->time_base = av_make_q(1, kOutSampleRate);
        st.ac->thread_count = 1;
        if (st.oc->oformat->flags & AVFMT_GLOBALHEADER)
            st.ac->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        re = avcodec_open2(st.ac, acodec, nullptr);
        if (re < 0) {
            emit finished(false, QStringLiteral("avcodec_open2(aac): %1").arg(avErrStr(re)));
            return false;
        }
        re = avcodec_parameters_from_context(st.as->codecpar, st.ac);
        if (re < 0) {
            emit finished(false, QStringLiteral("avcodec_parameters_from_context(aac): %1")
                                     .arg(avErrStr(re)));
            return false;
        }
        st.as->time_base = av_make_q(1, kOutSampleRate);

        // mixed s16 stereo 48k -> encoder sample format
        AVChannelLayout stereo2;
        av_channel_layout_default(&stereo2, kOutChannels);
        re = swr_alloc_set_opts2(&st.mixSwr,
                                 &stereo2, aFmt, kOutSampleRate,
                                 &stereo2, AV_SAMPLE_FMT_S16, kOutSampleRate,
                                 0, nullptr);
        if (re < 0 || !st.mixSwr || swr_init(st.mixSwr) < 0) {
            emit finished(false, QStringLiteral("swr_alloc_set_opts2(mix): %1").arg(avErrStr(re)));
            return false;
        }
    }

    // ---- open output file, write header ------------------------------------
    if (!(st.oc->oformat->flags & AVFMT_NOFILE)) {
        re = avio_open(&st.oc->pb, settings.outPath.toUtf8().constData(),
                       AVIO_FLAG_WRITE);
        if (re < 0) {
            emit finished(false, QStringLiteral("avio_open(%1): %2")
                                     .arg(settings.outPath, avErrStr(re)));
            return false;
        }
        st.ioOpened = true;
    }
    re = avformat_write_header(st.oc, nullptr);
    if (re < 0) {
        emit finished(false, QStringLiteral("avformat_write_header: %1").arg(avErrStr(re)));
        return false;
    }
    st.headerWritten = true;

    // ---- reusable frames / packets / sws ------------------------------------
    st.pkt = av_packet_alloc();
    st.vframe = av_frame_alloc();
    if (!st.pkt || !st.vframe) {
        emit finished(false, QStringLiteral("av_packet/av_frame alloc failed"));
        return false;
    }
    st.vframe->format = AV_PIX_FMT_YUV420P;
    st.vframe->width = W;
    st.vframe->height = H;
    re = av_frame_get_buffer(st.vframe, 32);
    if (re < 0) {
        emit finished(false, QStringLiteral("av_frame_get_buffer(video): %1").arg(avErrStr(re)));
        return false;
    }
    st.rgb2yuv = sws_getContext(W, H, AV_PIX_FMT_RGB24,
                               W, H, AV_PIX_FMT_YUV420P,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!st.rgb2yuv) {
        emit finished(false, QStringLiteral("sws_getContext(rgb2yuv) failed"));
        return false;
    }

    const int frameSize = (st.ac && st.ac->frame_size > 0) ? int(st.ac->frame_size) : 1024;
    if (st.ac) {
        st.aframe = av_frame_alloc();
        if (!st.aframe) {
            emit finished(false, QStringLiteral("av_frame_alloc(aframe) failed"));
            return false;
        }
        st.aframe->format = aFmt;
        re = av_channel_layout_copy(&st.aframe->ch_layout, &st.ac->ch_layout);
        if (re < 0) {
            emit finished(false, QStringLiteral("av_channel_layout_copy(aframe): %1")
                                     .arg(avErrStr(re)));
            return false;
        }
        st.aframe->sample_rate = kOutSampleRate;
        st.aframe->nb_samples = frameSize;
        re = av_frame_get_buffer(st.aframe, 0);
        if (re < 0) {
            emit finished(false, QStringLiteral("av_frame_get_buffer(audio): %1")
                                     .arg(avErrStr(re)));
            return false;
        }
    }

    // ---- mixing / scaling scratch ------------------------------------------
    QString errMsg;
    qint64 framesDone = 0;
    const int64_t outSamples = int64_t(std::ceil(total * kOutSampleRate - 1e-9));
    const int64_t nChunks = st.as ? (outSamples + frameSize - 1) / frameSize : 0;
    int64_t nextChunk = 0;
    int64_t apts = 0;                 // audio pts in 1/48000 (input samples)

    std::vector<int32_t> mix(static_cast<size_t>(2) * frameSize, 0);
    std::vector<int32_t> mixCnt(static_cast<size_t>(frameSize), 0);
    std::vector<int16_t> mixed(static_cast<size_t>(2) * frameSize, 0);
    std::vector<int16_t> clipPcm;
    std::vector<int16_t> resampled(size_t(4) * frameSize + 64);

    QImage outRgb(W, H, QImage::Format_RGB888);
    if (outRgb.isNull()) {
        emit finished(false, QStringLiteral("Cannot allocate %1x%2 canvas").arg(W).arg(H));
        return false;
    }
    QImage fitted;                    // scratch for the scaled source frame

    auto fail = [&](const QString &m) -> bool { errMsg = m; return false; };

    // Mix one audio chunk [ta, tb): pull every overlapping clip's PCM,
    // resample to 48k stereo, average, hand to the AAC encoder.
    auto writeAudioChunk = [&]() -> bool {
        const double ta = rangeStart + double(nextChunk) * frameSize / double(kOutSampleRate);
        const double tb = ta + double(frameSize) / double(kOutSampleRate);
        std::fill(mix.begin(), mix.end(), 0);
        std::fill(mixCnt.begin(), mixCnt.end(), 0);
        for (LocalClip &lc : clips) {
            if (!lc.audio || !lc.swr || !lc.dec)
                continue;
            const double cs = lc.timelineStart;
            const double ce = cs + lc.duration;
            if (ce <= ta || cs >= tb)
                continue;
            const double from = std::max(ta, cs);
            const double to = std::min(tb, ce);
            clipPcm.clear();
            lc.dec->decodeAudio(lc.sourceIn + (from - cs), to - from, clipPcm);
            const int64_t inFrames = lc.srcCh > 0
                                          ? int64_t(clipPcm.size()) / lc.srcCh : 0;
            if (inFrames <= 0)
                continue;
            const int64_t delayOut = swr_get_delay(lc.swr, kOutSampleRate);
            const int64_t need = delayOut +
                av_rescale_rnd(inFrames, kOutSampleRate, lc.srcRate, AV_ROUND_UP) + 64;
            if (int64_t(resampled.size()) < size_t(need) * 2)
                resampled.resize(size_t(need) * 2);
            uint8_t *outPtr = reinterpret_cast<uint8_t *>(resampled.data());
            const uint8_t *in[1] = { reinterpret_cast<const uint8_t *>(clipPcm.data()) };
            const int got = swr_convert(lc.swr, &outPtr, int(need), in, int(inFrames));
            if (got <= 0)
                continue;
            const int64_t start = int64_t((from - ta) * kOutSampleRate + 0.5);
            const int64_t stop = std::min<int64_t>(got, int64_t(frameSize) - start);
            for (int64_t j = 0; j < stop; ++j) {
                const int64_t idx = start + j;
                mix[size_t(2 * idx)] += resampled[size_t(2 * j)];
                mix[size_t(2 * idx + 1)] += resampled[size_t(2 * j + 1)];
                mixCnt[size_t(idx)] += 1;
            }
        }
        for (int i = 0; i < frameSize; ++i) {
            int32_t l = 0, r = 0;
            if (mixCnt[size_t(i)] > 0) {
                l = mix[size_t(2 * i)] / mixCnt[size_t(i)];
                r = mix[size_t(2 * i + 1)] / mixCnt[size_t(i)];
            }
            mixed[size_t(2 * i)] = int16_t(qBound(-32768, int(l), 32767));
            mixed[size_t(2 * i + 1)] = int16_t(qBound(-32768, int(r), 32767));
        }
        if (av_frame_make_writable(st.aframe) < 0)
            return fail(QStringLiteral("av_frame_make_writable(audio) failed"));
        st.aframe->nb_samples = frameSize;
        st.aframe->pts = apts;
        const uint8_t *in[1] = { reinterpret_cast<const uint8_t *>(mixed.data()) };
        const int got = swr_convert(st.mixSwr, st.aframe->extended_data,
                                    frameSize, in, frameSize);
        if (got < 0)
            return fail(QStringLiteral("swr_convert(mix) failed"));
        if (got < frameSize) {
            // pad the tail with silence (only the final chunk can be short)
            const int bps = av_get_bytes_per_sample(aFmt);
            if (av_sample_fmt_is_planar(aFmt)) {
                for (int c = 0; c < kOutChannels; ++c)
                    memset(st.aframe->extended_data[c] + size_t(got) * bps, 0,
                           size_t(frameSize - got) * bps);
            } else {
                memset(st.aframe->extended_data[0] + size_t(got) * kOutChannels * bps, 0,
                       size_t(frameSize - got) * kOutChannels * bps);
            }
        }
        if (!encodeFrame(st, st.ac, st.as, st.aframe, &errMsg))
            return false;
        apts += frameSize;
        ++nextChunk;
        return true;
    };

    // Render output frame i (timeline time t): topmost clip via clipAt,
    // effects, aspect-preserving scale, letterbox, yuv420p, encode.
    auto renderFrame = [&](qint64 i) -> bool {
        const double t = rangeStart + double(i) * frameDt;
        // keep audio ahead of (or at) the video time so packets interleave
        while (st.as && nextChunk < nChunks &&
               rangeStart + double(nextChunk) * frameSize / double(kOutSampleRate) <= t + 1e-9) {
            if (!writeAudioChunk())
                return false;
        }
        memset(outRgb.bits(), 0, size_t(outRgb.sizeInBytes()));   // black base
        const Clip *clip = nullptr;
        int track = -1;
        if (model.clipAt(t, &clip, &track) && clip) {
            Decoder *d = st.decoders.value(clip->path, nullptr);
            if (d) {
                const double srcT = clip->sourceIn + (t - clip->timelineStart);
                const ClipEffects fx = clip->fx;   // copy before decode
                double pts = 0.0;
                QImage img = d->frameAt(srcT, pts);
                if (!img.isNull()) {
                    if (settings.useEffects)
                        applyEffects(img, fx);
                    const QImage srcRgb =
                        img.format() == QImage::Format_RGB888
                            ? img : img.convertToFormat(QImage::Format_RGB888);
                    // aspect-preserving fit, even dims for the chroma planes
                    const double s = std::min(double(W) / srcRgb.width(),
                                             double(H) / srcRgb.height());
                    const int fw = std::max(2, qRound(srcRgb.width() * s)) & ~1;
                    const int fh = std::max(2, qRound(srcRgb.height() * s)) & ~1;
                    const QString key = QStringLiteral("%1x%2>%3x%4")
                                            .arg(srcRgb.width()).arg(srcRgb.height())
                                            .arg(fw).arg(fh);
                    SwsContext *sc = st.scaleCtxs.value(key, nullptr);
                    if (!sc) {
                        sc = sws_getContext(srcRgb.width(), srcRgb.height(), AV_PIX_FMT_RGB24,
                                            fw, fh, AV_PIX_FMT_RGB24,
                                            SWS_BILINEAR, nullptr, nullptr, nullptr);
                        if (!sc)
                            return fail(QStringLiteral("sws_getContext(scale) failed"));
                        st.scaleCtxs.insert(key, sc);
                    }
                    if (fitted.width() != fw || fitted.height() != fh)
                        fitted = QImage(fw, fh, QImage::Format_RGB888);
                    const uint8_t *srcSlice[1] = { srcRgb.constBits() };
                    const int srcStride[1] = { srcRgb.bytesPerLine() };
                    uint8_t *dstSlice[1] = { fitted.bits() };
                    const int dstStride[1] = { fitted.bytesPerLine() };
                    sws_scale(sc, srcSlice, srcStride, 0, srcRgb.height(),
                              dstSlice, dstStride);
                    const int offX = (W - fw) / 2;
                    const int offY = (H - fh) / 2;
                    for (int y = 0; y < fh; ++y)
                        memcpy(outRgb.scanLine(offY + y) + size_t(offX) * 3,
                               fitted.scanLine(y), size_t(fw) * 3);
                }
            }
        }
        if (av_frame_make_writable(st.vframe) < 0)
            return fail(QStringLiteral("av_frame_make_writable(video) failed"));
        const uint8_t *srcSlice[1] = { outRgb.constBits() };
        const int srcStride[1] = { outRgb.bytesPerLine() };
        sws_scale(st.rgb2yuv, srcSlice, srcStride, 0, H,
                  st.vframe->data, st.vframe->linesize);
        st.vframe->pts = i;
        if (!encodeFrame(st, st.vc, st.vs, st.vframe, &errMsg))
            return false;
        return true;
    };

    // ---- main loops ---------------------------------------------------------
    bool ok = true;
    for (qint64 i = 0; i < N; ++i) {
        if (cancelled_)
            break;
        if (!renderFrame(i)) { ok = false; break; }
        framesDone = i + 1;
        emit progress(framesDone);
    }
    if (ok) {   // audio tail past the last video frame time
        while (st.as && nextChunk < nChunks) {
            if (cancelled_)
                break;
            if (!writeAudioChunk()) { ok = false; break; }
        }
    }

    // ---- flush encoders + trailer (also on cancel: valid partial file) ------
    if (st.headerWritten) {
        QString flushErr;
        bool f = encodeFrame(st, st.vc, st.vs, nullptr, &flushErr);
        if (f && st.ac)
            f = encodeFrame(st, st.ac, st.as, nullptr, &flushErr);
        if (f) {
            const int r2 = av_write_trailer(st.oc);
            if (r2 < 0) {
                f = false;
                flushErr = QStringLiteral("av_write_trailer: %1").arg(avErrStr(r2));
            }
        }
        if (!f && ok) {   // keep the original error if the body already failed
            ok = false;
            errMsg = flushErr;
        }
    }

    const bool wasCancelled = cancelled_.load();
    if (ok && !wasCancelled) {
        emit finished(true, QStringLiteral("Exported %1 frames to %2")
                                .arg(N).arg(settings.outPath));
        return true;
    }
    if (wasCancelled)
        emit finished(false, QStringLiteral("Cancelled at frame %1 of %2")
                                    .arg(framesDone).arg(N));
    else
        emit finished(false, errMsg.isEmpty() ? QStringLiteral("Export failed") : errMsg);
    return false;
}