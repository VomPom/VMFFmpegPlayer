#ifndef FFMPEG_PLAYER_FF_READ_THREAD_H
#define FFMPEG_PLAYER_FF_READ_THREAD_H

#include <thread>
#include <atomic>
#include <functional>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavutil/avutil.h>
}

#include "ff_demuxer.h"
#include "ff_video_decoder.h"
#include "ff_audio_decoder.h"
#include "ff_packet_queue.h"
#include "ff_player_state.h"
#include "ff_timeline.h"
#include "ff_media_source.h"

/**
 * FFReadThread - 读取线程
 *
 * 职责：
 *   1. 从 FFDemuxer 读取 AVPacket
 *   2. 通过 BSF 进行 AVCC → AnnexB 转换（H.264/HEVC）
 *   3. 转换时间基为微秒，并进行 PTS 映射（源文件 → 时间线）
 *   4. 分发到视频/音频 PacketQueue
 *   5. 处理 seek 请求（包括暂停状态下的预读）
 *   6. 多片段串联：当一个片段 EOF 或超出裁剪范围时，自动切换到下一个片段
 *
 * 统一使用时间线模式：单片段播放视为只有一个片段的时间线。
 *
 * 片段切换回调：
 *   当切换到新片段时，通过 onClipSwitch 回调通知 FFPlayerContext，
 *   以便更新原子解码器指针（不同片段可能有不同的编码参数）。
 */

/**
 * 片段切换回调函数类型
 * @param clipIndex 新片段在 Track 中的索引
 * @param source 新片段的 MediaSource（已打开）
 */
using ClipSwitchCallback = std::function<void(int clipIndex, FFMediaSource *source)>;

class FFReadThread {
public:
    FFReadThread();
    ~FFReadThread();

    /**
     * 启动读取线程
     * @param timeline 项目时间线（单片段模式下也构建为只有一个 clip 的时间线）
     * @param nativeWindow 视频输出窗口
     * @param videoQueue 视频包队列
     * @param audioQueue 音频包队列
     * @param onClipSwitch 片段切换回调
     * @param state 播放状态引用
     * @param abortRequest 中止标志引用
     * @param seekRequest seek 请求标志引用
     * @param seekPositionUs seek 目标位置引用（时间线坐标）
     * @param seekDoneWhilePaused 暂停 seek 完成标志引用
     * @param seekTargetUs 精确 seek 目标引用（时间线坐标）
     */
    void start(const Timeline &timeline,
               ANativeWindow *nativeWindow,
               FFPacketQueue *videoQueue,
               FFPacketQueue *audioQueue,
               ClipSwitchCallback onClipSwitch,
               std::atomic<FFPlayerState> &state,
               std::atomic<bool> &abortRequest,
               std::atomic<bool> &seekRequest,
               std::atomic<int64_t> &seekPositionUs,
               std::atomic<bool> &seekDoneWhilePaused,
               std::atomic<int64_t> &seekTargetUs);

    /** 等待线程结束 */
    void join();

    /** 线程是否可 join */
    bool joinable() const { return thread_.joinable(); }

    /**
     * 获取当前活跃的 MediaSource
     * 线程安全：仅在片段切换回调中或线程结束后调用
     */
    FFMediaSource *getCurrentSource() { return currentSource_; }

private:
    std::thread thread_;

    // 当前 MediaSource
    FFMediaSource *currentSource_ = nullptr;

    /** 读取线程主循环 */
    void threadFunc(Timeline timeline,
                    ANativeWindow *nativeWindow,
                    FFPacketQueue *videoQueue,
                    FFPacketQueue *audioQueue,
                    ClipSwitchCallback onClipSwitch,
                    std::atomic<FFPlayerState> &state,
                    std::atomic<bool> &abortRequest,
                    std::atomic<bool> &seekRequest,
                    std::atomic<int64_t> &seekPositionUs,
                    std::atomic<bool> &seekDoneWhilePaused,
                    std::atomic<int64_t> &seekTargetUs);

    /** 处理暂停状态下 seek 后的视频包预读 */
    void handlePausedSeekPreread(FFMediaSource *source,
                                 FFPacketQueue *videoQueue);

    /**
     * 从 MediaSource 读取一个包，进行 BSF 过滤和 PTS 映射
     * @param source 当前媒体源
     * @param videoQueue 视频包队列
     * @param audioQueue 音频包队列
     * @return 0 成功分发, 1 片段结束（EOF 或超出裁剪范围）, 负值错误
     */
    int readAndDispatchPacket(FFMediaSource *source,
                              FFPacketQueue *videoQueue,
                              FFPacketQueue *audioQueue);
};

#endif // FFMPEG_PLAYER_FF_READ_THREAD_H
