#include "decoder.h"
#include <QDebug>

static QString avErr(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

Decoder::Decoder(const MediaInfo &info) : info_(info) {}

Decoder::~Decoder() {
    if (pkt_) av_packet_free(&pkt_);
    if (frame_) av_frame_free(&frame_);
    if (sws_) sws_freeContext(sws_);
    if (vctx_) avcodec_free_context(&vctx_);
    if (actx_) avcodec_free_context(&actx_);
    if (swr_) swr_free(&swr_);
    if (afmt_) avformat_close_input(&afmt_);
    if (fmt_) avformat_close_input(&fmt_);
}

bool Decoder::ensureOpen() {
    if (open_) return true;
    int err = avformat_open_input(&fmt_, info_.path.toUtf8().constData(), nullptr, nullptr);
    if (err < 0) { qWarning() << "open failed" << info_.path << avErr(err); return false; }
    err = avformat_find_stream_info(fmt_, nullptr);
    if (err < 0) { qWarning() << "find_stream_info failed" << avErr(err); return false; }
    openStreams();
    open_ = videoStream_ >= 0 && vctx_;
    return open_;
}

void Decoder::openStreams() {
    for (unsigned i = 0; i < fmt_->nb_streams; ++i) {
        AVStream *st = fmt_->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStream_ < 0) {
            videoStream_ = int(i);
            const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
            vctx_ = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(vctx_, st->codecpar);
            vctx_->pkt_timebase = st->time_base;
            // NOTE: frame threading (thread_count=0) breaks decode in some
            // environments (this VM). Slice threading is safe; revisit on
            // bare metal where auto threading may be fine.
            vctx_->thread_count = 1;
            vctx_->thread_type = 2; // FF_THREAD_SLICE
            avcodec_open2(vctx_, codec, nullptr);
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStream_ < 0) {
            audioStream_ = int(i);
        }
    }
    pkt_ = av_packet_alloc();
    frame_ = av_frame_alloc();
}

// Keyframe-seek to just before t, then decode forward until we cover t.
// Used for jumps too big for sequential decode (scrubs, replay, etc).
QImage Decoder::seekDecode(double t, double &ptsOut) {
    AVStream *vs = fmt_->streams[videoStream_];
    int64_t target = int64_t(t / av_q2d(vs->time_base));
    avformat_seek_file(fmt_, videoStream_, INT64_MIN, target, target, 0);
    avcodec_flush_buffers(vctx_);
    cache_.clear();
    eof_ = false;
    nextPts_ = -1.0;

    const double tol = frameDuration() * 0.25;
    int guard = 0;
    while (cache_.isEmpty() || cache_.last().pts < t - tol) {
        if (!decodeNext()) break; // EOF or decode failure
        if (guard++ > 100000) break; // safety
    }
    if (cache_.isEmpty()) return QImage(); // decode failed after seek

    for (int i = cache_.size() - 1; i >= 0; --i) {
        if (cache_[i].pts <= t) {
            ptsOut = cache_[i].pts;
            return cache_[i].image;
        }
    }
    ptsOut = cache_.first().pts;
    return cache_.first().image;
}

void Decoder::dropFramesBefore(double t) {
    // Trim from the front, but never drop the frame we'd return for t
    // (keep one frame with pts <= t) and keep a couple of frames of slack.
    while (cache_.size() > 2 && cache_[0].pts < t - frameDuration() &&
           cache_[1].pts <= t) {
        cache_.removeFirst();
    }
    // hard cap: never hold more than kMaxCache decoded frames
    while (cache_.size() > kMaxCache) cache_.removeFirst();
}

// Decode one more video frame; returns false on EOF.
bool Decoder::decodeNext() {
    if (eof_ || !open_) return false;
    AVStream *vs = fmt_->streams[videoStream_];
    bool drainedEof = false;
    for (;;) {
        int err = av_read_frame(fmt_, pkt_);
        if (err < 0) {
            eof_ = true;
            drainedEof = true;
            avcodec_send_packet(vctx_, nullptr); // flush decoders
        } else if (pkt_->stream_index == videoStream_) {
            // feed the packet to the decoder (must unref after send)
            avcodec_send_packet(vctx_, pkt_);
        } else {
            av_packet_unref(pkt_);
            continue;
        }

        // Drain everything the decoder now has.
        bool gotFrame = false;
        while (avcodec_receive_frame(vctx_, frame_) == 0) {
            double realPts;
            if (frame_->best_effort_timestamp != AV_NOPTS_VALUE) {
                realPts = frame_->best_effort_timestamp * av_q2d(vs->time_base);
            } else {
                realPts = nextPts_ >= 0 ? nextPts_ : 0.0;
                nextPts_ = realPts + frameDuration();
            }
            if (nextPts_ < 0) nextPts_ = realPts;
            nextPts_ = qMax(nextPts_, realPts + frameDuration());

            if (!sws_) {
                sws_ = sws_getContext(frame_->width, frame_->height, AVPixelFormat(frame_->format),
                                      frame_->width, frame_->height, AV_PIX_FMT_RGB32,
                                      SWS_BILINEAR, nullptr, nullptr, nullptr);
            }
            QImage img(frame_->width, frame_->height, QImage::Format_RGB32);
            uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
            int linesize[4] = {int(img.bytesPerLine()), 0, 0, 0};
            sws_scale(sws_, frame_->data, frame_->linesize, 0, frame_->height, dst, linesize);
            cache_.append({realPts, img.copy()});
            if (cache_.size() > kMaxCache) dropFramesBefore(realPts);
            gotFrame = true;
        }
        if (pkt_) av_packet_unref(pkt_);
        if (gotFrame) return true;
        if (drainedEof) return false;
    }
}

QImage Decoder::frameAt(double t, double &ptsOut) {
    if (!ensureOpen()) return QImage();

    // 1) cache hit: last frame with pts <= t, when the next frame is past t
    //    — no decode needed. A hit on the newest cached frame only counts
    //    when t is within one frame of it (otherwise decode forward).
    int hit = -1;
    for (int i = cache_.size() - 1; i >= 0; --i) {
        if (cache_[i].pts <= t) { hit = i; break; }
    }
    if (hit >= 0) {
        bool covered = hit + 1 < cache_.size() && cache_[hit + 1].pts > t;
        bool edge = hit + 1 >= cache_.size() && t - cache_[hit].pts < frameDuration();
        if (covered || edge) {
            ptsOut = cache_[hit].pts;
            return cache_[hit].image;
        }
    }

    // 2) near current decode position (or short jump): sequential decode
    const double tol = frameDuration() * 0.25;
    if (cache_.isEmpty() ||
        (cache_.last().pts >= 0 && t - cache_.last().pts <= kSeekGap) ||
        eof_) {
        int guard = 0;
        while (cache_.isEmpty() || cache_.last().pts < t - tol) {
            if (!decodeNext()) {
                if (cache_.isEmpty()) return QImage();
                break; // EOF: clamp to last frame
            }
            if (guard++ > 100000) break; // safety
            if (!cache_.isEmpty() && cache_.last().pts >= t - tol) break;
        }

        // pick frame: last with pts <= t (clamp at EOF)
        for (int i = cache_.size() - 1; i >= 0; --i) {
            if (cache_[i].pts <= t) {
                ptsOut = cache_[i].pts;
                return cache_[i].image;
            }
        }
    }

    // 3) big jump backwards (or cache gap): keyframe-seek + forward decode
    return seekDecode(t, ptsOut);
}
// ---- audio ----------------------------------------------------------------

// Open a dedicated AVFormatContext for audio demuxing. The video path owns
// fmt_ and seeks it constantly; sharing would corrupt both streams.
bool Decoder::ensureAudioOpen() {
    if (audioStream_ < 0) {
        // index not yet found: probe once via the main context
        if (!ensureOpen()) return false;
        for (unsigned i = 0; i < fmt_->nb_streams; ++i) {
            if (fmt_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                audioStream_ = int(i);
                break;
            }
        }
        if (audioStream_ < 0) return false;
    }
    if (actx_ && afmt_) return true;

    // dedicated demux context for audio
    afmt_ = avformat_alloc_context();
    if (avformat_open_input(&afmt_, info_.path.toUtf8().constData(), nullptr, nullptr) < 0) {
        avformat_free_context(afmt_);
        afmt_ = nullptr;
        audioStream_ = -1;
        return false;
    }
    avformat_find_stream_info(afmt_, nullptr);

    AVStream *st = afmt_->streams[audioStream_];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) return false;
    actx_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(actx_, st->codecpar);
    actx_->pkt_timebase = st->time_base;
    if (avcodec_open2(actx_, codec, nullptr) < 0) {
        avcodec_free_context(&actx_);
        audioStream_ = -1;
        return false;
    }
    sampleRate_ = actx_->sample_rate;
    channels_ = actx_->ch_layout.nb_channels;
    return true;
}

void Decoder::resetAudioRange() {
    pcm_.clear();
    pcm_.shrink_to_fit();
    audioDecodedUpTo_ = -1.0;
    audioRangeStart_ = -1.0;
}

// Decode s16le interleaved PCM for [from, from+maxSec). Reuses pcm_ when the
// requested window is already covered; seeks + re-decodes otherwise.
int64_t Decoder::decodeAudio(double from, double maxSec, std::vector<int16_t> &out) {
    if (!ensureAudioOpen()) return -1;
    if (maxSec <= 0) return 0;

    const double covered = audioDecodedUpTo_ >= 0 && audioRangeStart_ >= 0 &&
                           from >= audioRangeStart_ && from + maxSec <= audioDecodedUpTo_;
    if (!covered) {
        // seek to a bit before the requested point, then decode forward
        int64_t target = int64_t(from * actx_->sample_rate) * actx_->time_base.den /
                         (actx_->time_base.num * actx_->sample_rate);
        avformat_seek_file(afmt_, audioStream_, INT64_MIN, target, target, 0);
        avcodec_flush_buffers(actx_);
        pcm_.clear();
        audioRangeStart_ = from;
        audioDecodedUpTo_ = from;

        const double until = from + maxSec;
        bool eofAudio = false;
        while (audioDecodedUpTo_ < until && !eofAudio) {
            bool fed = false;
            while (!fed) {
                int err = av_read_frame(afmt_, pkt_);
                if (err < 0) {
                    avcodec_send_packet(actx_, nullptr); // flush
                    eofAudio = true;
                    break;
                }
                if (pkt_->stream_index == audioStream_) {
                    avcodec_send_packet(actx_, pkt_);
                    av_packet_unref(pkt_);
                    fed = true;
                } else {
                    av_packet_unref(pkt_);
                }
            }
            while (avcodec_receive_frame(actx_, frame_) == 0) {
                if (!swr_) {
                    AVChannelLayout outCh;
                    av_channel_layout_default(&outCh, channels_);
                    swr_alloc_set_opts2(&swr_,
                                        &outCh, AV_SAMPLE_FMT_S16, sampleRate_,
                                        &actx_->ch_layout, actx_->sample_fmt, actx_->sample_rate,
                                        0, nullptr);
                    swr_init(swr_);
                }
                uint8_t *dstPtr = nullptr;
                int dstSamples = av_rescale_rnd(
                    swr_get_delay(swr_, actx_->sample_rate) + frame_->nb_samples,
                    sampleRate_, actx_->sample_rate, AV_ROUND_UP);
                size_t prevSize = pcm_.size();
                pcm_.resize(prevSize + size_t(dstSamples) * channels_);
                dstPtr = reinterpret_cast<uint8_t *>(pcm_.data() + prevSize);
                int got = swr_convert(swr_, &dstPtr, dstSamples,
                                      frame_->extended_data, frame_->nb_samples);
                if (got > 0) {
                    pcm_.resize(prevSize + size_t(got) * channels_);
                    double frameDur = double(frame_->nb_samples) / actx_->sample_rate;
                    audioDecodedUpTo_ += frameDur;
                } else {
                    pcm_.resize(prevSize);
                }
                av_frame_unref(frame_);
            }
        }
    }

    // slice out [from, from+maxSec)
    int64_t startSample = int64_t((from - audioRangeStart_) * sampleRate_);
    if (startSample < 0) startSample = 0;
    int64_t want = int64_t(maxSec * sampleRate_);
    int64_t available = int64_t(pcm_.size()) / channels_ - startSample;
    int64_t n = qBound<int64_t>(0, qMin(want, available), INT64_MAX);
    if (n <= 0) return 0;
    out.insert(out.end(), pcm_.begin() + startSample * channels_,
               pcm_.begin() + (startSample + n) * channels_);
    return n;
}
