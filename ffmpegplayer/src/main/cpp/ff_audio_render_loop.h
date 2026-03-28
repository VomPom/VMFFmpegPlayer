#ifndef FFMPEG_PLAYER_FF_AUDIO_RENDER_LOOP_H
#define FFMPEG_PLAYER_FF_AUDIO_RENDER_LOOP_H

#include <thread>
#include <atomic>

#include "ff_audio_decoder.h"
#include "ff_packet_queue.h"
#include "ff_player_state.h"

/**
 * FFAudioRenderLoop - 音频渲染循环
 *
 * 职责：
 *   1. 从音频 PacketQueue 取包并送入解码器
 *   2. 解码音频帧并推送到播放缓冲队列
 *   3. 处理 EOF 排空
 *
 * 统一使用原子指针获取解码器，单片段视为只有一个片段的多片段，
 * 片段切换时外部更新原子指针即可，渲染线程无需重启。
 */
class FFAudioRenderLoop {
public:
    FFAudioRenderLoop();
    ~FFAudioRenderLoop();

    /**
     * 启动音频渲染线程
     * 通过原子指针获取解码器，支持片段切换时动态替换
     */
    void start(std::atomic<FFAudioDecoder *> &audioDecoderPtr,
               FFPacketQueue *audioQueue,
               std::atomic<FFPlayerState> &state,
               std::atomic<bool> &abortRequest);

    /** 等待线程结束 */
    void join();

    /** 线程是否可 join */
    bool joinable() const { return thread_.joinable(); }

private:
    std::thread thread_;

    /** 音频渲染线程主循环 */
    void threadFunc(std::atomic<FFAudioDecoder *> &audioDecoderPtr,
                    FFPacketQueue *audioQueue,
                    std::atomic<FFPlayerState> &state,
                    std::atomic<bool> &abortRequest);
};

#endif // FFMPEG_PLAYER_FF_AUDIO_RENDER_LOOP_H
