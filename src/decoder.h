#pragma once

#include <QString>
#include <QImage>
#include <QVector>
#include <QHash>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include <cstdint>
#include <vector>

struct MediaInfo {
    QString path;
    QString name;
    double duration = 0.0;   // seconds
    double fps = 25.0;
    int width = 0;
    int height = 0;
    bool hasVideo = false;
    bool hasAudio = false;
};

// One persistent decoder per source file. Keeps demux/decode context open
// across frame requests; caches recent frames; plays forward without
// re-seeking. All times are seconds from source start.
class Decoder {
public:
    explicit Decoder(const MediaInfo &info);
    ~Decoder();

    // Frame at or after time t. O(1) when playing sequentially from cache
    // window; keyframe-seek + forward decode otherwise.
    QImage frameAt(double t, double &ptsOut);

    // Convenience: nearest decoded frame image (no pts).
    QImage seekVideo(double t) { double pts; return frameAt(t, pts); }

    // --- audio ---
    bool hasAudio() const { return audioStream_ >= 0; }
    int sampleRate() const { return sampleRate_; }
    int channels() const { return channels_; }

    // Decode PCM s16 mono/stereo interleaved covering [from, from+maxSec).
    // Returns samples appended (interleaved s16le), or -1 on error.
    // Cheap when the requested range is already decoded (cached).
    int64_t decodeAudio(double from, double maxSec, std::vector<int16_t> &out);

    double duration() const { return info_.duration; }
    double fps() const { return info_.fps; }
    QString path() const { return info_.path; }

private:
    struct CachedFrame {
        double pts = 0.0;
        QImage image;
    };

    bool ensureOpen();
    void openStreams();
    bool decodeNext();               // decode one more video frame into cache
    void dropFramesBefore(double t); // keep cache window small
    QImage seekDecode(double t, double &ptsOut); // keyframe-seek + forward decode
    double frameDuration() const { return info_.fps > 0 ? 1.0 / info_.fps : 0.04; }

    bool ensureAudioOpen();
    void resetAudioRange();

    MediaInfo info_;
    bool open_ = false;

    AVFormatContext *fmt_ = nullptr;
    int videoStream_ = -1;
    AVCodecContext *vctx_ = nullptr;
    SwsContext *sws_ = nullptr;

    // audio decode state — separate AVFormatContext so video seeks
    // don't clobber the audio demux position mid-playback
    int audioStream_ = -1;
    AVFormatContext *afmt_ = nullptr;   // opened lazily on first audio request
    AVCodecContext *actx_ = nullptr;
    SwrContext *swr_ = nullptr;
    int sampleRate_ = 0;
    int channels_ = 0;
    double audioDecodedUpTo_ = -1.0;  // seconds of PCM held in pcm_
    double audioRangeStart_ = -1.0;   // start of pcm_ buffer
    std::vector<int16_t> pcm_;        // interleaved s16le samples

    AVPacket *pkt_ = nullptr;
    AVFrame *frame_ = nullptr;

    // Sorted by pts (monotonic during forward decode).
    QVector<CachedFrame> cache_;
    double nextPts_ = -1.0;      // pts of next decoded frame (from packet)
    bool eof_ = false;

    static constexpr int kMaxCache = 24;    // ~1s at 24fps; bounds RAM (~200MB @1080p)
    static constexpr double kSeekGap = 2.0; // decode-forward below this gap; keyframe-seek beyond
};