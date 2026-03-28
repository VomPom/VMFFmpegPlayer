#ifndef FFMPEG_PLAYER_FF_PLAYER_CONTEXT_H
#define FFMPEG_PLAYER_FF_PLAYER_CONTEXT_H

#include <jni.h>
#include <string>
#include <mutex>
#include <atomic>
#include <memory>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include "ff_demuxer.h"
#include "ff_video_decoder.h"
#include "ff_audio_decoder.h"
#include "ff_av_sync.h"
#include "ff_packet_queue.h"
#include "ff_player_callback.h"
#include "ff_jni_callback.h"
#include "ff_read_thread.h"
#include "ff_video_render_loop.h"
#include "ff_audio_render_loop.h"
#include "effect_pipeline.h"
#include "speed_effect.h"
#include "ff_player_state.h"
#include "ff_timeline.h"
#include "ff_media_source.h"

/**
 * FFPlayerContext - 播放器核心协调者
 *
 * 职责（单一职责）：
 *   1. 播放状态机管理
 *   2. 子模块生命周期管理（创建/销毁）
 *   3. 协调各模块之间的交互
 *
 * 统一使用 Timeline 模型：
 *   - prepare(path) / prepareWithFd(fd) 会自动构建只有一个片段的 Timeline
 *   - prepareTimeline(timeline) 直接使用多片段 Timeline
 *   - start/stop/pause/resume/seek 等操作完全统一，无分支
 *
 * 具体的线程逻辑已拆分到独立模块：
 *   - FFReadThread: 读取线程（解封装 + BSF + 分发 + 多片段切换）
 *   - FFVideoRenderLoop: 视频线程（解码 + 同步 + 特效 + 渲染）
 *   - FFAudioRenderLoop: 音频线程（解码 + 播放）
 *   - FFPlayerCallback / FFJniCallback: 回调机制（解耦 JNI）
 *   - FFPacketQueue: 线程安全包队列（独立数据结构）
 *   - FFMediaSource: 单片段媒体源封装（Demuxer + BSF + Decoder）
 */
class FFPlayerContext {
public:
    FFPlayerContext(JavaVM *javaVM, jobject javaPlayer);
    ~FFPlayerContext();

    // ==================== 播放控制 ====================
    int prepare(const std::string &path);
    int prepareWithFd(int fd);
    int prepareTimeline(const Timeline &timeline);

    void start();
    void pause();
    void resume();
    void stop();
    void seekTo(int64_t positionMs);
    void release();
    void reset();

    // ==================== Surface 管理 ====================
    void setSurface(JNIEnv *env, jobject surface);

    // ==================== 信息查询 ====================
    int64_t getDuration();
    int64_t getCurrentPosition();
    int getState();
    int getVideoWidth();
    int getVideoHeight();

    // ==================== 特效控制 ====================
    void setSpeed(float speed);
    float getSpeed();
    EffectPipeline *getEffectPipeline() { return effectPipeline_; }

private:
    // ==================== 回调 ====================
    FFJniCallback *callback_ = nullptr;

    // ==================== 时间线（统一模型） ====================
    Timeline timeline_;
    // 原子指针：读取线程在片段切换时更新，渲染线程通过原子读取获取最新解码器
    std::atomic<FFVideoDecoder *> videoDecoderPtr_{nullptr};
    std::atomic<FFAudioDecoder *> audioDecoderPtr_{nullptr};
    // 当前活跃的 MediaSource（由读取线程管理生命周期）
    std::atomic<FFMediaSource *> currentSource_{nullptr};

    // ==================== 共享子模块 ====================
    FFAVSync *avSync_ = nullptr;

    // 特效管线
    EffectPipeline *effectPipeline_ = nullptr;
    std::shared_ptr<SpeedEffect> speedEffect_;

    // ==================== 线程模块 ====================
    FFReadThread readThread_;
    FFVideoRenderLoop videoRenderLoop_;
    FFAudioRenderLoop audioRenderLoop_;

    // ==================== 包队列 ====================
    FFPacketQueue videoQueue_;
    FFPacketQueue audioQueue_;

    // ==================== 状态控制 ====================
    std::atomic<FFPlayerState> state_{FFPlayerState::IDLE};
    std::atomic<bool> abortRequest_{false};
    std::atomic<bool> seekRequest_{false};
    std::atomic<int64_t> seekPositionUs_{0};
    std::atomic<bool> seekDoneWhilePaused_{false};
    std::atomic<int64_t> seekTargetUs_{-1};

    // ==================== Surface ====================
    ANativeWindow *nativeWindow_ = nullptr;
    std::mutex surfaceMutex_;

    // ==================== 辅助方法 ====================
    void releaseSubModules();
    void initEffectPipeline();

    /**
     * 内部统一的 prepare 实现
     * 构建 Timeline 后设置状态为 PREPARED
     */
    int prepareInternal(const Timeline &timeline);

    /**
     * 通过 Demuxer 探测源文件信息，构建单片段 Timeline
     * @param path 文件路径（path 和 fd 二选一）
     * @param fd 文件描述符（-1 表示使用 path）
     * @return 构建好的 Timeline，失败时 empty() 为 true
     */
    Timeline buildSingleClipTimeline(const std::string &path, int fd = -1);

    /**
     * 片段切换回调（由读取线程调用）
     * 更新原子解码器指针，使渲染线程能获取到新片段的解码器
     */
    void onClipSwitched(int clipIndex, FFMediaSource *source);
};

#endif // FFMPEG_PLAYER_FF_PLAYER_CONTEXT_H