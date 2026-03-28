#ifndef FFMPEG_PLAYER_FF_VIDEO_RENDER_LOOP_H
#define FFMPEG_PLAYER_FF_VIDEO_RENDER_LOOP_H

#include <thread>
#include <atomic>

#include "ff_video_decoder.h"
#include "ff_audio_decoder.h"
#include "ff_av_sync.h"
#include "ff_packet_queue.h"
#include "ff_player_callback.h"
#include "effect_pipeline.h"
#include "ff_player_state.h"

/**
 * FFVideoRenderLoop - 视频渲染循环
 *
 * 职责：
 *   1. 从视频 PacketQueue 取包并送入解码器
 *   2. 通过特效管线处理视频帧时间轴
 *   3. 基于音频时钟进行 AV 同步
 *   4. 控制帧率并渲染到 Surface
 *   5. 处理暂停状态下的 seek 预览
 *
 * 统一使用原子指针获取解码器，单片段视为只有一个片段的多片段，
 * 片段切换时外部更新原子指针即可，渲染线程无需重启。
 */
class FFVideoRenderLoop {
public:
    FFVideoRenderLoop();
    ~FFVideoRenderLoop();

    /**
     * 启动视频渲染线程
     * 通过原子指针获取解码器，支持片段切换时动态替换
     */
    void start(std::atomic<FFVideoDecoder *> &videoDecoderPtr,
               std::atomic<FFAudioDecoder *> &audioDecoderPtr,
               FFAVSync *avSync,
               EffectPipeline *effectPipeline,
               FFPacketQueue *videoQueue,
               FFPlayerCallback *callback,
               std::atomic<FFPlayerState> &state,
               std::atomic<bool> &abortRequest,
               std::atomic<bool> &seekDoneWhilePaused,
               std::atomic<int64_t> &seekTargetUs);

    /** 等待线程结束 */
    void join();

    /** 线程是否可 join */
    bool joinable() const { return thread_.joinable(); }

private:
    std::thread thread_;

    /** 视频渲染线程主循环 */
    void threadFunc(std::atomic<FFVideoDecoder *> &videoDecoderPtr,
                    std::atomic<FFAudioDecoder *> &audioDecoderPtr,
                    FFAVSync *avSync,
                    EffectPipeline *effectPipeline,
                    FFPacketQueue *videoQueue,
                    FFPlayerCallback *callback,
                    std::atomic<FFPlayerState> &state,
                    std::atomic<bool> &abortRequest,
                    std::atomic<bool> &seekDoneWhilePaused,
                    std::atomic<int64_t> &seekTargetUs);

    /** 暂停状态下 seek 后解码渲染一帧（拖动预览） */
    void handlePauseSeekPreview(FFVideoDecoder *videoDecoder,
                                FFPacketQueue *videoQueue,
                                std::atomic<bool> &abortRequest,
                                std::atomic<int64_t> &seekTargetUs);
};

#endif // FFMPEG_PLAYER_FF_VIDEO_RENDER_LOOP_H
