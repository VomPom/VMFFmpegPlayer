#include "ff_audio_render_loop.h"
#include <unistd.h>

#define LOG_TAG "FFAudioRenderLoop"
#include "ff_log.h"

FFAudioRenderLoop::FFAudioRenderLoop() = default;

FFAudioRenderLoop::~FFAudioRenderLoop() = default;

void FFAudioRenderLoop::start(std::atomic<FFAudioDecoder *> &audioDecoderPtr,
                               FFPacketQueue *audioQueue,
                               std::atomic<FFPlayerState> &state,
                               std::atomic<bool> &abortRequest) {
    thread_ = std::thread(&FFAudioRenderLoop::threadFunc, this,
                           std::ref(audioDecoderPtr), audioQueue,
                           std::ref(state), std::ref(abortRequest));
}

void FFAudioRenderLoop::join() {
    if (thread_.joinable()) {
        LOGI("Waiting for audio thread to finish");
        thread_.join();
        LOGI("Audio thread finished");
    }
}

void FFAudioRenderLoop::threadFunc(std::atomic<FFAudioDecoder *> &audioDecoderPtr,
                                    FFPacketQueue *audioQueue,
                                    std::atomic<FFPlayerState> &state,
                                    std::atomic<bool> &abortRequest) {
    LOGD("Audio thread started");

    while (!abortRequest.load()) {
        // 每次循环都从原子指针获取最新的解码器
        FFAudioDecoder *audioDecoder = audioDecoderPtr.load();

        if (!audioDecoder) {
            usleep(5000);
            continue;
        }

        if (state.load() == FFPlayerState::PAUSED) {
            usleep(10000);
            continue;
        }

        // 从队列取包
        AVPacket *packet = audioQueue->pop();
        if (!packet) {
            if (audioQueue->eof.load() && audioQueue->size() == 0) {
                audioDecoder = audioDecoderPtr.load();
                if (audioDecoder) {
                    audioDecoder->sendPacket(nullptr);
                    while (!abortRequest.load()) {
                        int ret = audioDecoder->decodeFrame();
                        if (ret < 0) break;
                    }
                }
                break;
            }
            usleep(2000);
            continue;
        }

        // 重新获取解码器（可能在等待期间发生了片段切换）
        audioDecoder = audioDecoderPtr.load();
        if (!audioDecoder) {
            av_packet_free(&packet);
            continue;
        }

        // 送入解码器
        audioDecoder->sendPacket(packet);
        av_packet_free(&packet);

        // 接收解码帧并推送到播放队列
        while (!abortRequest.load()) {
            audioDecoder = audioDecoderPtr.load();
            if (!audioDecoder) break;
            int ret = audioDecoder->decodeFrame();
            if (ret != 0) break;
        }
    }

    LOGD("Audio thread exited");
}