#include "ff_player_context.h"
#include <unistd.h>

#define LOG_TAG "FFPlayerContext"
#include "ff_log.h"

// ==================== 构造/析构 ====================

FFPlayerContext::FFPlayerContext(JavaVM *javaVM, jobject javaPlayer) {
    callback_ = new FFJniCallback(javaVM, javaPlayer);
}

FFPlayerContext::~FFPlayerContext() {
    release();
}

// ==================== 子模块生命周期 ====================

void FFPlayerContext::releaseSubModules() {
    // 解码器由 MediaSource 管理，ReadThread 负责释放
    // 这里只需清空原子指针
    videoDecoderPtr_.store(nullptr);
    audioDecoderPtr_.store(nullptr);
    currentSource_.store(nullptr);

    if (avSync_) {
        delete avSync_;
        avSync_ = nullptr;
    }
    if (effectPipeline_) {
        delete effectPipeline_;
        effectPipeline_ = nullptr;
    }
    speedEffect_.reset();
}

void FFPlayerContext::initEffectPipeline() {
    if (!effectPipeline_) {
        effectPipeline_ = new EffectPipeline();
        speedEffect_ = std::make_shared<SpeedEffect>();
        effectPipeline_->addEffect(speedEffect_);
        LOGD("特效管线已初始化，内置变速特效已注册");
    }
}

// ==================== 片段切换回调 ====================

void FFPlayerContext::onClipSwitched(int clipIndex, FFMediaSource *source) {
    LOGI("片段切换: clipIndex=%d, source=%p", clipIndex, source);

    // 更新原子指针，渲染线程会在下次循环时获取到新的解码器
    videoDecoderPtr_.store(source ? source->getVideoDecoder() : nullptr);
    audioDecoderPtr_.store(source ? source->getAudioDecoder() : nullptr);
    currentSource_.store(source);

    // 通知视频尺寸变化（不同片段可能有不同分辨率）
    if (source && source->getVideoDecoder() && callback_) {
        callback_->onVideoSizeChanged(source->getWidth(), source->getHeight());
    }

    // 同步音频速度设置到新的解码器
    if (source && source->getAudioDecoder() && speedEffect_) {
        source->getAudioDecoder()->setSpeed(speedEffect_->getSpeedFactor());
    }
}

// ==================== 单片段 Timeline 构建 ====================

Timeline FFPlayerContext::buildSingleClipTimeline(const std::string &path, int fd) {
    Timeline timeline;

    // 临时打开 demuxer 探测文件信息
    FFDemuxer probeDemuxer;
    int ret;
    if (fd >= 0) {
        ret = probeDemuxer.openFd(fd);
    } else {
        ret = probeDemuxer.open(path.c_str());
    }

    if (ret < 0) {
        LOGE("Failed to open file for probing: %s (fd=%d)", path.c_str(), fd);
        return timeline; // 返回空 timeline
    }

    int64_t durationUs = probeDemuxer.getDurationUs();

    // 构建单片段 ClipDescriptor
    ClipDescriptor clip;
    clip.sourcePath = path;
    clip.fd = fd;
    clip.srcStartUs = 0;
    clip.srcEndUs = 0;  // 0 表示到文件末尾
    clip.timelineStartUs = 0;
    clip.timelineEndUs = durationUs;

    Track mainTrack;
    mainTrack.trackId = 0;
    mainTrack.zOrder = 0;
    mainTrack.clips.push_back(clip);

    timeline.tracks.push_back(mainTrack);
    timeline.totalDurationUs = durationUs;

    // 通知视频尺寸（从探测 demuxer 获取）
    if (probeDemuxer.getVideoStreamIndex() >= 0 && callback_) {
        callback_->onVideoSizeChanged(probeDemuxer.getWidth(), probeDemuxer.getHeight());
    }

    LOGI("Built single-clip timeline: path=%s, duration=%lldms, video=%dx%d, fps=%.1f",
         path.c_str(),
         (long long) (durationUs / 1000),
         probeDemuxer.getWidth(), probeDemuxer.getHeight(),
         probeDemuxer.getVideoFps());

    probeDemuxer.close();
    return timeline;
}

// ==================== 内部统一 prepare ====================

int FFPlayerContext::prepareInternal(const Timeline &timeline) {
    if (timeline.empty()) {
        LOGE("prepareInternal: timeline is empty");
        callback_->onError(-1, "Timeline is empty");
        state_.store(FFPlayerState::ERROR);
        return -1;
    }

    timeline_ = timeline;

    // 创建 AV 同步器
    avSync_ = new FFAVSync();

    // 初始化特效管线
    initEffectPipeline();

    state_.store(FFPlayerState::PREPARED);
    callback_->onStateChanged((int) FFPlayerState::PREPARED);
    callback_->onPrepared(timeline_.totalDurationUs / 1000);

    LOGI("Prepare done: %d clips, totalDuration=%lldms",
         timeline_.clipCount(), (long long) (timeline_.totalDurationUs / 1000));

    return 0;
}

// ==================== 播放控制 ====================

int FFPlayerContext::prepare(const std::string &path) {
    FFPlayerState curState = state_.load();
    if (curState != FFPlayerState::IDLE && curState != FFPlayerState::STOPPED) {
        LOGE("prepare: invalid state: %d", (int) curState);
        return -1;
    }

    if (curState == FFPlayerState::STOPPED) {
        releaseSubModules();
    }

    state_.store(FFPlayerState::PREPARING);
    callback_->onStateChanged((int) FFPlayerState::PREPARING);

    // 构建单片段 Timeline
    Timeline timeline = buildSingleClipTimeline(path);
    if (timeline.empty()) {
        callback_->onError(-1, "Failed to open file");
        state_.store(FFPlayerState::ERROR);
        return -1;
    }

    return prepareInternal(timeline);
}

int FFPlayerContext::prepareWithFd(int fd) {
    FFPlayerState curState = state_.load();
    if (curState != FFPlayerState::IDLE && curState != FFPlayerState::STOPPED) {
        return -1;
    }

    if (curState == FFPlayerState::STOPPED) {
        releaseSubModules();
    }

    state_.store(FFPlayerState::PREPARING);
    callback_->onStateChanged((int) FFPlayerState::PREPARING);

    // 构建单片段 Timeline（通过 fd）
    Timeline timeline = buildSingleClipTimeline("", fd);
    if (timeline.empty()) {
        callback_->onError(-1, "Failed to open file via fd");
        state_.store(FFPlayerState::ERROR);
        return -1;
    }

    return prepareInternal(timeline);
}

int FFPlayerContext::prepareTimeline(const Timeline &timeline) {
    FFPlayerState curState = state_.load();
    if (curState != FFPlayerState::IDLE && curState != FFPlayerState::STOPPED) {
        LOGE("prepareTimeline: invalid state: %d", (int) curState);
        return -1;
    }

    if (curState == FFPlayerState::STOPPED) {
        releaseSubModules();
    }

    state_.store(FFPlayerState::PREPARING);
    callback_->onStateChanged((int) FFPlayerState::PREPARING);

    return prepareInternal(timeline);
}

void FFPlayerContext::start() {
    FFPlayerState expected = FFPlayerState::PREPARED;
    if (!state_.compare_exchange_strong(expected, FFPlayerState::PLAYING)) {
        expected = FFPlayerState::PAUSED;
        if (!state_.compare_exchange_strong(expected, FFPlayerState::PLAYING)) {
            LOGE("start: invalid state %d", (int) state_.load());
            return;
        }
        resume();
        return;
    }

    abortRequest_.store(false);
    callback_->onStateChanged((int) FFPlayerState::PLAYING);

    ANativeWindow *window = nullptr;
    {
        std::lock_guard<std::mutex> lock(surfaceMutex_);
        window = nativeWindow_;
    }

    // 启动读取线程
    readThread_.start(
        timeline_, window,
        &videoQueue_, &audioQueue_,
        [this](int clipIndex, FFMediaSource *source) {
            onClipSwitched(clipIndex, source);
        },
        state_, abortRequest_,
        seekRequest_, seekPositionUs_,
        seekDoneWhilePaused_, seekTargetUs_);

    // 等待第一个片段的解码器就绪
    int waitCount = 0;
    while (!videoDecoderPtr_.load() && !audioDecoderPtr_.load()
           && !abortRequest_.load() && waitCount < 200) {
        usleep(5000); // 5ms
        waitCount++;
    }

    // 启动视频渲染线程
    if (videoDecoderPtr_.load()) {
        videoRenderLoop_.start(
            videoDecoderPtr_, audioDecoderPtr_,
            avSync_, effectPipeline_,
            &videoQueue_, callback_,
            state_, abortRequest_,
            seekDoneWhilePaused_, seekTargetUs_);
    }

    // 启动音频渲染线程
    if (audioDecoderPtr_.load()) {
        audioRenderLoop_.start(
            audioDecoderPtr_,
            &audioQueue_,
            state_, abortRequest_);
    }

    LOGI("Playback started (%d clips)", timeline_.clipCount());
}

void FFPlayerContext::pause() {
    FFPlayerState expected = FFPlayerState::PLAYING;
    if (!state_.compare_exchange_strong(expected, FFPlayerState::PAUSED)) {
        return;
    }

    FFAudioDecoder *ad = audioDecoderPtr_.load();
    if (ad) ad->pause();

    callback_->onStateChanged((int) FFPlayerState::PAUSED);
    LOGI("Playback paused");
}

void FFPlayerContext::resume() {
    FFPlayerState expected = FFPlayerState::PAUSED;
    if (!state_.compare_exchange_strong(expected, FFPlayerState::PLAYING)) {
        return;
    }

    FFAudioDecoder *ad = audioDecoderPtr_.load();
    if (ad) ad->resume();

    callback_->onStateChanged((int) FFPlayerState::PLAYING);
    LOGI("Playback resumed");
}

void FFPlayerContext::stop() {
    FFPlayerState curState = state_.load();
    if (curState == FFPlayerState::STOPPED || curState == FFPlayerState::IDLE) {
        return;
    }

    LOGI("Stopping playback, current state: %d", (int) curState);

    abortRequest_.store(true);

    // 中止当前 MediaSource
    FFMediaSource *source = currentSource_.load();
    if (source) source->abort();

    FFAudioDecoder *ad = audioDecoderPtr_.load();
    if (ad) ad->abort();

    videoQueue_.abort();
    audioQueue_.abort();

    // 等待所有线程结束
    readThread_.join();
    videoRenderLoop_.join();
    audioRenderLoop_.join();

    videoQueue_.flush();
    audioQueue_.flush();

    state_.store(FFPlayerState::STOPPED);
    callback_->onStateChanged((int) FFPlayerState::STOPPED);
    LOGI("Playback stopped");
}

void FFPlayerContext::seekTo(int64_t positionMs) {
    int64_t targetUs = positionMs * 1000;
    seekPositionUs_.store(targetUs);
    seekTargetUs_.store(targetUs);
    seekRequest_.store(true);
    LOGI("Seek to %lld ms (target %lld us)", (long long) positionMs, (long long) targetUs);
}

void FFPlayerContext::reset() {
    LOGI("Resetting player state");

    stop();

    abortRequest_.store(false);
    seekRequest_.store(false);
    seekPositionUs_.store(0);
    seekDoneWhilePaused_.store(false);
    seekTargetUs_.store(-1);

    videoQueue_.flush();
    audioQueue_.flush();

    videoDecoderPtr_.store(nullptr);
    audioDecoderPtr_.store(nullptr);
    currentSource_.store(nullptr);

    if (avSync_) avSync_->reset();
    if (effectPipeline_) effectPipeline_->resetAll();

    state_.store(FFPlayerState::PREPARED);
    callback_->onStateChanged((int) FFPlayerState::PREPARED);

    LOGI("Player reset complete, state set to PREPARED");
}

void FFPlayerContext::release() {
    if (state_.load() == FFPlayerState::PLAYING || state_.load() == FFPlayerState::PAUSED) {
        stop();
    }

    FFMediaSource *source = currentSource_.load();
    if (source) source->abort();

    releaseSubModules();

    {
        std::lock_guard<std::mutex> lock(surfaceMutex_);
        if (nativeWindow_) {
            ANativeWindow_release(nativeWindow_);
            nativeWindow_ = nullptr;
        }
    }

    // 释放回调（释放 Java 全局引用）
    if (callback_) {
        callback_->release();
        delete callback_;
        callback_ = nullptr;
    }

    state_.store(FFPlayerState::IDLE);
    LOGI("Player released");
}

void FFPlayerContext::setSurface(JNIEnv *env, jobject surface) {
    std::lock_guard<std::mutex> lock(surfaceMutex_);

    if (nativeWindow_) {
        ANativeWindow_release(nativeWindow_);
        nativeWindow_ = nullptr;
    }

    if (surface) {
        nativeWindow_ = ANativeWindow_fromSurface(env, surface);
        LOGD("Set Surface: window=%p", nativeWindow_);
        // Surface 变化会在下次片段切换或重新 start 时自动生效
        // （MediaSource 在 open 时使用最新的 nativeWindow_）
    } else {
        LOGD("Surface cleared");
    }
}

// ==================== 信息查询 ====================

int64_t FFPlayerContext::getDuration() {
    return timeline_.totalDurationUs / 1000;
}

int64_t FFPlayerContext::getCurrentPosition() {
    FFAudioDecoder *ad = audioDecoderPtr_.load();
    return ad ? ad->getAudioClockUs() / 1000 : 0;
}

int FFPlayerContext::getState() {
    return (int) state_.load();
}

int FFPlayerContext::getVideoWidth() {
    FFMediaSource *source = currentSource_.load();
    return source ? source->getWidth() : 0;
}

int FFPlayerContext::getVideoHeight() {
    FFMediaSource *source = currentSource_.load();
    return source ? source->getHeight() : 0;
}

// ==================== 特效控制 ====================

void FFPlayerContext::setSpeed(float speed) {
    if (speedEffect_) {
        speedEffect_->setSpeed(speed);
        LOGI("播放速度设置为: %.2f", speed);
    }
    FFAudioDecoder *ad = audioDecoderPtr_.load();
    if (ad) ad->setSpeed(speed);
}

float FFPlayerContext::getSpeed() {
    return speedEffect_ ? speedEffect_->getSpeedFactor() : 1.0f;
}