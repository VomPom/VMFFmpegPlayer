#include "ff_demuxer.h"
#include <android/log.h>

#define LOG_TAG "FFDemuxer"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

FFDemuxer::FFDemuxer() {
}

FFDemuxer::~FFDemuxer() {
    close();
}

int FFDemuxer::open(const char *path) {
    std::lock_guard<std::mutex> lock(readMutex);
    resetAbort();

    bool isNetwork = isNetworkUrl(path);
    AVDictionary *opts = nullptr;

    if (isNetwork) {
        // 为网络流分配 context 并设置中断回调
        formatCtx = avformat_alloc_context();
        if (!formatCtx) {
            LOGE("Failed to alloc AVFormatContext");
            return AVERROR(ENOMEM);
        }
        formatCtx->interrupt_callback.callback = interruptCallback;
        formatCtx->interrupt_callback.opaque = this;

        // 网络超时及重连参数
        av_dict_set(&opts, "timeout", "10000000", 0);       // 连接超时 10 秒（微秒）
        av_dict_set(&opts, "reconnect", "1", 0);             // 断线自动重连
        av_dict_set(&opts, "reconnect_streamed", "1", 0);    // 流式传输也重连
        av_dict_set(&opts, "reconnect_delay_max", "5", 0);   // 最大重连延迟 5 秒
        LOGD("Opening network URL: %s", path);
    }

    int ret = avformat_open_input(&formatCtx, path, nullptr, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        char errBuf[128];
        av_strerror(ret, errBuf, sizeof(errBuf));
        LOGE("Failed to open file: %s, error: %s", path, errBuf);
        return ret;
    }

    ret = avformat_find_stream_info(formatCtx, nullptr);
    if (ret < 0) {
        LOGE("Failed to get stream info");
        avformat_close_input(&formatCtx);
        return ret;
    }

    // Find video and audio streams
    for (unsigned int i = 0; i < formatCtx->nb_streams; i++) {
        AVCodecParameters *params = formatCtx->streams[i]->codecpar;
        if (params->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIndex < 0) {
            videoStreamIndex = (int) i;
            LOGD("Found video stream: index=%d, codec=%d, %dx%d",
                 i, params->codec_id, params->width, params->height);
        } else if (params->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIndex < 0) {
            audioStreamIndex = (int) i;
            LOGD("Found audio stream: index=%d, codec=%d, sampleRate=%d, channels=%d",
                 i, params->codec_id, params->sample_rate, params->ch_layout.nb_channels);
        }
    }

    if (videoStreamIndex < 0) {
        LOGE("Video stream not found");
        return -1;
    }

    LOGD("File opened successfully, duration: %lld us, video stream: %d, audio stream: %d",
         (long long) formatCtx->duration, videoStreamIndex, audioStreamIndex);

    return 0;
}

int FFDemuxer::openFd(int fd) {
    std::lock_guard<std::mutex> lock(readMutex);

    // Build pipe path from fd
    char fdPath[64];
    snprintf(fdPath, sizeof(fdPath), "pipe:%d", fd);

    // Inline implementation to avoid recursive lock issues
    int ret = avformat_open_input(&formatCtx, fdPath, nullptr, nullptr);
    if (ret < 0) {
        // Try /proc/self/fd/ approach
        snprintf(fdPath, sizeof(fdPath), "/proc/self/fd/%d", fd);
        ret = avformat_open_input(&formatCtx, fdPath, nullptr, nullptr);
        if (ret < 0) {
            char errBuf[128];
            av_strerror(ret, errBuf, sizeof(errBuf));
            LOGE("Failed to open file via fd: %d, error: %s", fd, errBuf);
            return ret;
        }
    }

    ret = avformat_find_stream_info(formatCtx, nullptr);
    if (ret < 0) {
        LOGE("Failed to get stream info (fd)");
        avformat_close_input(&formatCtx);
        return ret;
    }

    for (unsigned int i = 0; i < formatCtx->nb_streams; i++) {
        AVCodecParameters *params = formatCtx->streams[i]->codecpar;
        if (params->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIndex < 0) {
            videoStreamIndex = (int) i;
        } else if (params->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIndex < 0) {
            audioStreamIndex = (int) i;
        }
    }

    if (videoStreamIndex < 0) {
        LOGE("Video stream not found (fd)");
        return -1;
    }

    LOGD("File opened via fd successfully, duration: %lld us", (long long) formatCtx->duration);
    return 0;
}

int FFDemuxer::readPacket(AVPacket *packet) {
    std::lock_guard<std::mutex> lock(readMutex);
    if (!formatCtx) return -1;

    int ret = av_read_frame(formatCtx, packet);
    return ret;
}

int FFDemuxer::seek(int64_t timestampUs) {
    std::lock_guard<std::mutex> lock(readMutex);
    if (!formatCtx) return -1;

    int ret = av_seek_frame(formatCtx, -1, timestampUs, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        LOGE("Seek failed: %lld us", (long long) timestampUs);
    }
    return ret;
}

void FFDemuxer::close() {
    std::lock_guard<std::mutex> lock(readMutex);
    if (formatCtx) {
        avformat_close_input(&formatCtx);
        formatCtx = nullptr;
    }
    videoStreamIndex = -1;
    audioStreamIndex = -1;
}

AVCodecParameters *FFDemuxer::getVideoCodecParams() const {
    if (!formatCtx || videoStreamIndex < 0) return nullptr;
    return formatCtx->streams[videoStreamIndex]->codecpar;
}

AVCodecParameters *FFDemuxer::getAudioCodecParams() const {
    if (!formatCtx || audioStreamIndex < 0) return nullptr;
    return formatCtx->streams[audioStreamIndex]->codecpar;
}

int64_t FFDemuxer::getDurationUs() const {
    if (!formatCtx) return 0;
    return formatCtx->duration; // AV_TIME_BASE unit (microseconds)
}

int FFDemuxer::getWidth() const {
    auto *params = getVideoCodecParams();
    return params ? params->width : 0;
}

int FFDemuxer::getHeight() const {
    auto *params = getVideoCodecParams();
    return params ? params->height : 0;
}

double FFDemuxer::getVideoFps() const {
    if (!formatCtx || videoStreamIndex < 0) return 30.0;
    AVStream *stream = formatCtx->streams[videoStreamIndex];
    if (stream->avg_frame_rate.den > 0) {
        return av_q2d(stream->avg_frame_rate);
    }
    if (stream->r_frame_rate.den > 0) {
        return av_q2d(stream->r_frame_rate);
    }
    return 30.0;
}

int FFDemuxer::getSampleRate() const {
    auto *params = getAudioCodecParams();
    return params ? params->sample_rate : 44100;
}

int FFDemuxer::getChannels() const {
    auto *params = getAudioCodecParams();
    return params ? params->ch_layout.nb_channels : 2;
}

AVRational FFDemuxer::getVideoTimeBase() const {
    if (!formatCtx || videoStreamIndex < 0) return {1, 1000000};
    return formatCtx->streams[videoStreamIndex]->time_base;
}

AVRational FFDemuxer::getAudioTimeBase() const {
    if (!formatCtx || audioStreamIndex < 0) return {1, 1000000};
    return formatCtx->streams[audioStreamIndex]->time_base;
}

void FFDemuxer::abort() {
    aborted_.store(true);
    LOGD("Demuxer abort requested");
}

void FFDemuxer::resetAbort() {
    aborted_.store(false);
}

bool FFDemuxer::isNetworkUrl(const char *path) {
    if (!path) return false;
    return strncmp(path, "http://", 7) == 0 ||
           strncmp(path, "https://", 8) == 0 ||
           strncmp(path, "rtmp://", 7) == 0 ||
           strncmp(path, "rtsp://", 7) == 0 ||
           strncmp(path, "hls://", 6) == 0;
}

int FFDemuxer::interruptCallback(void *ctx) {
    auto *demuxer = static_cast<FFDemuxer *>(ctx);
    if (demuxer && demuxer->aborted_.load()) {
        LOGD("interruptCallback: aborted");
        return 1; // 返回 1 表示中断 IO
    }
    return 0;
}
