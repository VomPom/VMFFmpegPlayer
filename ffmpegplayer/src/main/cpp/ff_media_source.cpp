#include "ff_media_source.h"

#define LOG_TAG "FFMediaSource"
#include "ff_log.h"

FFMediaSource::FFMediaSource() = default;

FFMediaSource::~FFMediaSource() {
    close();
}

int FFMediaSource::open(const ClipDescriptor &clip, ANativeWindow *nativeWindow) {
    clip_ = clip;

    // 创建并打开 Demuxer
    demuxer_ = new FFDemuxer();
    int ret;
    if (!clip_.sourcePath.empty()) {
        ret = demuxer_->open(clip_.sourcePath.c_str());
    } else if (clip_.fd >= 0) {
        ret = demuxer_->openFd(clip_.fd);
    } else {
        LOGE("MediaSource: 无有效的源路径或 fd");
        delete demuxer_;
        demuxer_ = nullptr;
        return -1;
    }

    if (ret < 0) {
        LOGE("MediaSource: 打开源文件失败: %s", clip_.sourcePath.c_str());
        delete demuxer_;
        demuxer_ = nullptr;
        return ret;
    }

    // 初始化 BSF
    initBSF();

    // 创建视频解码器
    if (nativeWindow && demuxer_->getVideoStreamIndex() >= 0) {
        videoDecoder_ = new FFVideoDecoder();
        ret = videoDecoder_->init(demuxer_->getVideoCodecParams(), nativeWindow);
        if (ret < 0) {
            LOGE("MediaSource: 视频解码器初始化失败");
            delete videoDecoder_;
            videoDecoder_ = nullptr;
        }
    }

    // 创建音频解码器
    if (demuxer_->getAudioStreamIndex() >= 0) {
        audioDecoder_ = new FFAudioDecoder();
        ret = audioDecoder_->init(demuxer_->getAudioCodecParams());
        if (ret < 0) {
            LOGE("MediaSource: 音频解码器初始化失败");
            delete audioDecoder_;
            audioDecoder_ = nullptr;
        }
    }

    // 如果片段有裁剪起点，seek 到起始位置
    if (clip_.srcStartUs > 0) {
        seekToClipStart();
    }

    LOGI("MediaSource 已打开: src=%s, srcRange=[%lld, %lld)us, tlRange=[%lld, %lld)us",
         clip_.sourcePath.c_str(),
         (long long) clip_.srcStartUs, (long long) clip_.srcEndUs,
         (long long) clip_.timelineStartUs, (long long) clip_.timelineEndUs);

    return 0;
}

void FFMediaSource::close() {
    releaseBSF();

    if (videoDecoder_) {
        videoDecoder_->release();
        delete videoDecoder_;
        videoDecoder_ = nullptr;
    }
    if (audioDecoder_) {
        audioDecoder_->release();
        delete audioDecoder_;
        audioDecoder_ = nullptr;
    }
    if (demuxer_) {
        demuxer_->close();
        delete demuxer_;
        demuxer_ = nullptr;
    }
}

int FFMediaSource::initBSF() {
    if (!demuxer_ || demuxer_->getVideoStreamIndex() < 0) return 0;

    AVCodecParameters *vpar = demuxer_->getVideoCodecParams();
    const char *bsfName = nullptr;
    if (vpar->codec_id == AV_CODEC_ID_H264) {
        bsfName = "h264_mp4toannexb";
    } else if (vpar->codec_id == AV_CODEC_ID_HEVC) {
        bsfName = "hevc_mp4toannexb";
    }

    if (!bsfName) return 0;

    const AVBitStreamFilter *bsf = av_bsf_get_by_name(bsfName);
    if (!bsf) {
        LOGE("BSF not found: %s", bsfName);
        return -1;
    }

    int ret = av_bsf_alloc(bsf, &bsfCtx_);
    if (ret < 0) return ret;

    avcodec_parameters_copy(bsfCtx_->par_in, vpar);
    bsfCtx_->time_base_in = demuxer_->getVideoTimeBase();
    ret = av_bsf_init(bsfCtx_);
    if (ret < 0) {
        LOGE("BSF init failed: %d", ret);
        av_bsf_free(&bsfCtx_);
        bsfCtx_ = nullptr;
        return ret;
    }

    LOGD("BSF [%s] 初始化成功", bsfName);
    return 0;
}

void FFMediaSource::releaseBSF() {
    if (bsfCtx_) {
        av_bsf_free(&bsfCtx_);
        bsfCtx_ = nullptr;
    }
}

int FFMediaSource::getVideoStreamIndex() const {
    return demuxer_ ? demuxer_->getVideoStreamIndex() : -1;
}

int FFMediaSource::getAudioStreamIndex() const {
    return demuxer_ ? demuxer_->getAudioStreamIndex() : -1;
}

AVRational FFMediaSource::getVideoTimeBase() const {
    return demuxer_ ? demuxer_->getVideoTimeBase() : AVRational{1, AV_TIME_BASE};
}

AVRational FFMediaSource::getAudioTimeBase() const {
    return demuxer_ ? demuxer_->getAudioTimeBase() : AVRational{1, AV_TIME_BASE};
}

int FFMediaSource::getWidth() const {
    return demuxer_ ? demuxer_->getWidth() : 0;
}

int FFMediaSource::getHeight() const {
    return demuxer_ ? demuxer_->getHeight() : 0;
}

double FFMediaSource::getVideoFps() const {
    return demuxer_ ? demuxer_->getVideoFps() : 30.0;
}

int FFMediaSource::seekToClipStart() {
    if (!demuxer_) return -1;
    int ret = demuxer_->seek(clip_.srcStartUs);
    if (ret < 0) {
        LOGE("MediaSource: seek 到片段起点失败: %lld us", (long long) clip_.srcStartUs);
    }
    // flush 解码器
    if (videoDecoder_) videoDecoder_->flush();
    if (audioDecoder_) audioDecoder_->flush();
    return ret;
}

void FFMediaSource::abort() {
    if (demuxer_) {
        demuxer_->abort();
    }
    if (audioDecoder_) {
        audioDecoder_->abort();
    }
}
