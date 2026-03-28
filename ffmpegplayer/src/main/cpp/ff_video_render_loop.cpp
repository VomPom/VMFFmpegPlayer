#include "ff_video_render_loop.h"
#include <unistd.h>
#include <algorithm>

extern "C" {
#include <libavutil/time.h>
}

#define LOG_TAG "FFVideoRenderLoop"
#include "ff_log.h"

FFVideoRenderLoop::FFVideoRenderLoop() = default;

FFVideoRenderLoop::~FFVideoRenderLoop() = default;

void FFVideoRenderLoop::start(std::atomic<FFVideoDecoder *> &videoDecoderPtr,
                               std::atomic<FFAudioDecoder *> &audioDecoderPtr,
                               FFAVSync *avSync,
                               EffectPipeline *effectPipeline,
                               FFPacketQueue *videoQueue,
                               FFPlayerCallback *callback,
                               std::atomic<FFPlayerState> &state,
                               std::atomic<bool> &abortRequest,
                               std::atomic<bool> &seekDoneWhilePaused,
                               std::atomic<int64_t> &seekTargetUs) {
    thread_ = std::thread(&FFVideoRenderLoop::threadFunc, this,
                           std::ref(videoDecoderPtr), std::ref(audioDecoderPtr),
                           avSync, effectPipeline,
                           videoQueue, callback,
                           std::ref(state), std::ref(abortRequest),
                           std::ref(seekDoneWhilePaused), std::ref(seekTargetUs));
}

void FFVideoRenderLoop::join() {
    if (thread_.joinable()) {
        LOGI("Waiting for video thread to finish");
        thread_.join();
        LOGI("Video thread finished");
    }
}

void FFVideoRenderLoop::handlePauseSeekPreview(FFVideoDecoder *videoDecoder,
                                                FFPacketQueue *videoQueue,
                                                std::atomic<bool> &abortRequest,
                                                std::atomic<int64_t> &seekTargetUs) {
    int64_t pauseSeekTarget = seekTargetUs.load();
    bool rendered = false;

    // 从队列中取包、解码、精确定位到目标帧并渲染
    while (!abortRequest.load()) {
        AVPacket *pkt = videoQueue->pop();
        if (!pkt) break;
        int sendRet = videoDecoder->sendPacket(pkt);
        av_packet_free(&pkt);
        if (sendRet != 0) continue;

        // 尝试取出解码帧
        while (!abortRequest.load()) {
            int64_t framePts = 0;
            ssize_t frameBufIdx = -1;
            int recvRet = videoDecoder->dequeueFrame(framePts, frameBufIdx);
            if (recvRet != 0 || frameBufIdx < 0) break;

            // 精确 seek：PTS < 目标的帧只解码不渲染
            if (pauseSeekTarget >= 0 && framePts < pauseSeekTarget) {
                videoDecoder->releaseFrame(frameBufIdx, false);
                continue;
            }

            // PTS >= 目标，渲染这一帧
            videoDecoder->releaseFrame(frameBufIdx, true);
            rendered = true;
            seekTargetUs.store(-1);
            break;
        }
        if (rendered) break;
    }

    // 清空预读队列中剩余的包
    while (true) {
        AVPacket *remaining = videoQueue->pop();
        if (!remaining) break;
        av_packet_free(&remaining);
    }
}

void FFVideoRenderLoop::threadFunc(std::atomic<FFVideoDecoder *> &videoDecoderPtr,
                                    std::atomic<FFAudioDecoder *> &audioDecoderPtr,
                                    FFAVSync *avSync,
                                    EffectPipeline *effectPipeline,
                                    FFPacketQueue *videoQueue,
                                    FFPlayerCallback *callback,
                                    std::atomic<FFPlayerState> &state,
                                    std::atomic<bool> &abortRequest,
                                    std::atomic<bool> &seekDoneWhilePaused,
                                    std::atomic<int64_t> &seekTargetUs) {
    LOGD("Video thread started");

    // 帧率控制变量
    int64_t lastPtsUs = 0;

    while (!abortRequest.load()) {
        // 每次循环都从原子指针获取最新的解码器（片段切换时会更新）
        FFVideoDecoder *videoDecoder = videoDecoderPtr.load();
        FFAudioDecoder *audioDecoder = audioDecoderPtr.load();

        if (!videoDecoder) {
            usleep(5000);
            continue;
        }

        if (state.load() == FFPlayerState::PAUSED) {
            if (seekDoneWhilePaused.load()) {
                seekDoneWhilePaused.store(false);
                // 重新获取最新解码器（seek 可能触发了片段切换）
                videoDecoder = videoDecoderPtr.load();
                if (videoDecoder) {
                    handlePauseSeekPreview(videoDecoder, videoQueue, abortRequest, seekTargetUs);
                }
            }
            usleep(10000);
            continue;
        }

        // 从队列取包
        AVPacket *packet = videoQueue->pop();
        if (!packet) {
            if (videoQueue->eof.load() && videoQueue->size() == 0) {
                // 所有片段播放完毕
                videoDecoder->sendPacket(nullptr);
                while (!abortRequest.load()) {
                    int64_t ptsUs = 0;
                    int ret = videoDecoder->receiveFrame(ptsUs, false);
                    if (ret < 0) break;
                }
                break;
            }
            usleep(2000);
            continue;
        }

        // 重新获取解码器（可能在等待期间发生了片段切换）
        videoDecoder = videoDecoderPtr.load();
        audioDecoder = audioDecoderPtr.load();
        if (!videoDecoder) {
            av_packet_free(&packet);
            continue;
        }

        // 送入解码器
        int sendRet = videoDecoder->sendPacket(packet);
        av_packet_free(&packet);

        if (sendRet != 0) {
            usleep(1000);
            continue;
        }

        // 接收解码帧，进行 AV 同步后渲染
        while (!abortRequest.load()) {
            // 每次解码帧时也重新获取解码器指针
            videoDecoder = videoDecoderPtr.load();
            audioDecoder = audioDecoderPtr.load();
            if (!videoDecoder) break;

            int64_t ptsUs = 0;
            ssize_t bufIdx = -1;
            int recvRet = videoDecoder->dequeueFrame(ptsUs, bufIdx);

            if (recvRet != 0) break;
            if (bufIdx < 0) break;

            // 精确 seek：丢弃 PTS < seek 目标的帧
            int64_t curSeekTarget = seekTargetUs.load();
            if (curSeekTarget >= 0 && ptsUs < curSeekTarget) {
                videoDecoder->releaseFrame(bufIdx, false);
                continue;
            }
            if (curSeekTarget >= 0) {
                seekTargetUs.store(-1);
            }

            // ==================== 特效管线处理 ====================
            int64_t effectivePtsUs = ptsUs;
            float currentSpeed = 1.0f;
            if (effectPipeline) {
                TimeEffectResult timeResult;
                effectPipeline->processVideoTime(ptsUs, timeResult);
                effectivePtsUs = timeResult.adjustedPtsUs;
                currentSpeed = timeResult.speedFactor;

                if (timeResult.shouldDrop) {
                    videoDecoder->releaseFrame(bufIdx, false);
                    continue;
                }
            }

            // AV 同步决策
            bool shouldRender = true;
            if (avSync && audioDecoder) {
                int64_t audioClk = audioDecoder->getAudioClockUs();
                if (audioClk > 0) {
                    int64_t waitUs = 0;
                    auto action = avSync->sync(ptsUs, audioClk, waitUs);
                    if (action == FFAVSync::WAIT && waitUs > 0) {
                        int64_t remaining = std::min(waitUs, (int64_t) 100000);
                        while (remaining > 0 && !abortRequest.load()) {
                            int64_t sleepTime = std::min(remaining, (int64_t) 5000);
                            usleep((useconds_t) sleepTime);
                            remaining -= sleepTime;
                        }
                        shouldRender = true;
                    } else if (action == FFAVSync::DROP) {
                        shouldRender = false;
                    }
                } else {
                    // 音频时钟尚未更新，使用 PTS 帧率控制
                    if (lastPtsUs > 0 && ptsUs > lastPtsUs) {
                        int64_t frameDuration = ptsUs - lastPtsUs;
                        if (currentSpeed != 1.0f && currentSpeed > 0.0f) {
                            frameDuration = (int64_t) ((double) frameDuration / (double) currentSpeed);
                        }
                        if (frameDuration > 0 && frameDuration < 1000000) {
                            usleep((useconds_t) frameDuration);
                        }
                    }
                }
                lastPtsUs = ptsUs;
            } else {
                // 无音频流，基于帧率控制播放速度
                static int64_t lastFrameTime = av_gettime();
                int64_t currentTime = av_gettime();
                int64_t frameDuration = ptsUs - lastPtsUs;

                if (currentSpeed != 1.0f && currentSpeed > 0.0f) {
                    frameDuration = (int64_t) ((double) frameDuration / (double) currentSpeed);
                }

                if (frameDuration > 0 && frameDuration < 1000000) {
                    int64_t elapsed = currentTime - lastFrameTime;
                    int64_t sleepTime = frameDuration - elapsed;

                    if (sleepTime > 0 && sleepTime < 100000) {
                        usleep((useconds_t) sleepTime);
                    }
                }

                lastFrameTime = av_gettime();
                lastPtsUs = ptsUs;
            }

            // 渲染或丢弃帧
            videoDecoder->releaseFrame(bufIdx, shouldRender);
        }
    }

    // 检查是否播放完成（非用户主动停止）
    FFVideoDecoder *finalDecoder = videoDecoderPtr.load();
    FFAudioDecoder *finalAudioDecoder = audioDecoderPtr.load();
    if (!abortRequest.load() && finalDecoder && finalDecoder->isEOS()) {
        while (!abortRequest.load() && finalAudioDecoder && !finalAudioDecoder->isEOS()) {
            usleep(10000);
        }
        state.store(FFPlayerState::COMPLETED);
        if (callback) {
            callback->onStateChanged((int) FFPlayerState::COMPLETED);
            callback->onCompletion();
        }
    }

    LOGD("Video thread exited");
}