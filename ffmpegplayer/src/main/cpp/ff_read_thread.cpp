#include "ff_read_thread.h"
#include <unistd.h>

#define LOG_TAG "FFReadThread"
#include "ff_log.h"

FFReadThread::FFReadThread() = default;

FFReadThread::~FFReadThread() = default;

void FFReadThread::start(const Timeline &timeline,
                          ANativeWindow *nativeWindow,
                          FFPacketQueue *videoQueue,
                          FFPacketQueue *audioQueue,
                          ClipSwitchCallback onClipSwitch,
                          std::atomic<FFPlayerState> &state,
                          std::atomic<bool> &abortRequest,
                          std::atomic<bool> &seekRequest,
                          std::atomic<int64_t> &seekPositionUs,
                          std::atomic<bool> &seekDoneWhilePaused,
                          std::atomic<int64_t> &seekTargetUs) {
    thread_ = std::thread(&FFReadThread::threadFunc, this,
                           timeline, nativeWindow,
                           videoQueue, audioQueue,
                           std::move(onClipSwitch),
                           std::ref(state), std::ref(abortRequest),
                           std::ref(seekRequest), std::ref(seekPositionUs),
                           std::ref(seekDoneWhilePaused), std::ref(seekTargetUs));
}

void FFReadThread::join() {
    if (thread_.joinable()) {
        LOGI("Waiting for read thread to finish");
        thread_.join();
        LOGI("Read thread finished");
    }
}

int FFReadThread::readAndDispatchPacket(FFMediaSource *source,
                                         FFPacketQueue *videoQueue,
                                         FFPacketQueue *audioQueue) {
    FFDemuxer *demuxer = source->getDemuxer();
    AVBSFContext *bsf = source->getBSFContext();
    const ClipDescriptor &clip = source->getClip();

    int videoIdx = source->getVideoStreamIndex();
    int audioIdx = source->getAudioStreamIndex();
    AVRational videoTb = source->getVideoTimeBase();
    AVRational audioTb = source->getAudioTimeBase();
    AVRational usTb = {1, AV_TIME_BASE};

    AVPacket *packet = av_packet_alloc();
    int ret = demuxer->readPacket(packet);

    if (ret == AVERROR_EOF) {
        av_packet_free(&packet);
        return 1; // 片段结束
    }

    if (ret < 0) {
        av_packet_free(&packet);
        return ret; // 错误
    }

    if (packet->stream_index == videoIdx) {
        if (bsf) {
            int bsfRet = av_bsf_send_packet(bsf, packet);
            if (bsfRet < 0) {
                av_packet_free(&packet);
                return 0;
            }
            while (av_bsf_receive_packet(bsf, packet) == 0) {
                // 转换为微秒
                int64_t srcPtsUs = AV_NOPTS_VALUE;
                if (packet->pts != AV_NOPTS_VALUE) {
                    srcPtsUs = av_rescale_q(packet->pts, videoTb, usTb);
                }
                int64_t srcDtsUs = AV_NOPTS_VALUE;
                if (packet->dts != AV_NOPTS_VALUE) {
                    srcDtsUs = av_rescale_q(packet->dts, videoTb, usTb);
                }

                // 检查是否超出片段裁剪范围
                if (srcPtsUs != AV_NOPTS_VALUE && source->isSourcePtsBeyondClip(srcPtsUs)) {
                    av_packet_free(&packet);
                    return 1; // 片段结束
                }

                // PTS 映射：源文件 → 时间线
                if (srcPtsUs != AV_NOPTS_VALUE) {
                    packet->pts = clip.srcToTimelinePts(srcPtsUs);
                }
                if (srcDtsUs != AV_NOPTS_VALUE) {
                    packet->dts = clip.srcToTimelinePts(srcDtsUs);
                }

                videoQueue->push(packet);
                packet = av_packet_alloc();
            }
            av_packet_free(&packet);
        } else {
            int64_t srcPtsUs = AV_NOPTS_VALUE;
            if (packet->pts != AV_NOPTS_VALUE) {
                srcPtsUs = av_rescale_q(packet->pts, videoTb, usTb);
            }
            int64_t srcDtsUs = AV_NOPTS_VALUE;
            if (packet->dts != AV_NOPTS_VALUE) {
                srcDtsUs = av_rescale_q(packet->dts, videoTb, usTb);
            }

            if (srcPtsUs != AV_NOPTS_VALUE && source->isSourcePtsBeyondClip(srcPtsUs)) {
                av_packet_free(&packet);
                return 1;
            }

            if (srcPtsUs != AV_NOPTS_VALUE) {
                packet->pts = clip.srcToTimelinePts(srcPtsUs);
            }
            if (srcDtsUs != AV_NOPTS_VALUE) {
                packet->dts = clip.srcToTimelinePts(srcDtsUs);
            }

            videoQueue->push(packet);
        }
    } else if (packet->stream_index == audioIdx) {
        int64_t srcPtsUs = AV_NOPTS_VALUE;
        if (packet->pts != AV_NOPTS_VALUE) {
            srcPtsUs = av_rescale_q(packet->pts, audioTb, usTb);
        }
        int64_t srcDtsUs = AV_NOPTS_VALUE;
        if (packet->dts != AV_NOPTS_VALUE) {
            srcDtsUs = av_rescale_q(packet->dts, audioTb, usTb);
        }

        if (srcPtsUs != AV_NOPTS_VALUE && source->isSourcePtsBeyondClip(srcPtsUs)) {
            av_packet_free(&packet);
            return 1;
        }

        if (srcPtsUs != AV_NOPTS_VALUE) {
            packet->pts = clip.srcToTimelinePts(srcPtsUs);
        }
        if (srcDtsUs != AV_NOPTS_VALUE) {
            packet->dts = clip.srcToTimelinePts(srcDtsUs);
        }

        audioQueue->push(packet);
    } else {
        av_packet_free(&packet);
    }

    return 0;
}

void FFReadThread::handlePausedSeekPreread(FFMediaSource *source,
                                            FFPacketQueue *videoQueue) {
    int prereadCount = 0;
    while (prereadCount < 300) {
        FFPacketQueue dummyAudioQueue;
        int ret = readAndDispatchPacket(source, videoQueue, &dummyAudioQueue);
        if (ret != 0) break;
        prereadCount++;
        dummyAudioQueue.flush();
    }
    LOGD("Paused seek: preread %d packets", prereadCount);
}

void FFReadThread::threadFunc(Timeline timeline,
                               ANativeWindow *nativeWindow,
                               FFPacketQueue *videoQueue,
                               FFPacketQueue *audioQueue,
                               ClipSwitchCallback onClipSwitch,
                               std::atomic<FFPlayerState> &state,
                               std::atomic<bool> &abortRequest,
                               std::atomic<bool> &seekRequest,
                               std::atomic<int64_t> &seekPositionUs,
                               std::atomic<bool> &seekDoneWhilePaused,
                               std::atomic<int64_t> &seekTargetUs) {
    LOGD("Read thread started (%d clips)", timeline.clipCount());

    if (timeline.empty()) {
        LOGE("Timeline is empty, read thread exiting");
        return;
    }

    const Track &mainTrack = timeline.mainTrack();
    int currentClipIndex = 0;

    // 打开第一个片段
    auto *source = new FFMediaSource();
    int ret = source->open(mainTrack.clips[currentClipIndex], nativeWindow);
    if (ret < 0) {
        LOGE("Failed to open first clip");
        delete source;
        return;
    }
    currentSource_ = source;

    // 通知片段切换（第一个片段）
    if (onClipSwitch) {
        onClipSwitch(currentClipIndex, source);
    }

    while (!abortRequest.load()) {
        // ==================== 处理 seek 请求 ====================
        if (seekRequest.load()) {
            int64_t seekTimelinePos = seekPositionUs.load();
            bool wasPaused = (state.load() == FFPlayerState::PAUSED);

            // 根据时间线位置找到对应的片段
            int targetClipIdx = mainTrack.findClipIndex(seekTimelinePos);
            if (targetClipIdx < 0) {
                if (seekTimelinePos >= timeline.totalDurationUs) {
                    targetClipIdx = (int) mainTrack.clips.size() - 1;
                } else {
                    targetClipIdx = 0;
                }
            }

            // 如果需要切换片段
            if (targetClipIdx != currentClipIndex) {
                source->close();
                delete source;

                source = new FFMediaSource();
                ret = source->open(mainTrack.clips[targetClipIdx], nativeWindow);
                if (ret < 0) {
                    LOGE("Failed to open clip %d during seek", targetClipIdx);
                    delete source;
                    source = nullptr;
                    currentSource_ = nullptr;
                    break;
                }
                currentClipIndex = targetClipIdx;
                currentSource_ = source;

                if (onClipSwitch) {
                    onClipSwitch(currentClipIndex, source);
                }
            }

            // 在源文件中 seek 到对应位置
            const ClipDescriptor &clip = mainTrack.clips[currentClipIndex];
            int64_t srcSeekPos = clip.timelineToSrcPts(seekTimelinePos);
            source->getDemuxer()->seek(srcSeekPos);

            // 清空队列和刷新解码器
            videoQueue->flush();
            audioQueue->flush();
            if (source->getVideoDecoder()) source->getVideoDecoder()->flush();
            if (source->getAudioDecoder()) source->getAudioDecoder()->flush();

            // 暂停状态下 seek 后预读
            if (wasPaused && source->getVideoDecoder()) {
                handlePausedSeekPreread(source, videoQueue);
                seekDoneWhilePaused.store(true);
            }

            seekRequest.store(false);
        }

        // ==================== 检查暂停状态 ====================
        if (state.load() == FFPlayerState::PAUSED) {
            usleep(10000);
            continue;
        }

        // ==================== 控制队列大小 ====================
        if (videoQueue->size() > 64 || audioQueue->size() > 64) {
            usleep(10000);
            continue;
        }

        // ==================== 读取并分发包 ====================
        ret = readAndDispatchPacket(source, videoQueue, audioQueue);

        if (ret == 1) {
            // 当前片段结束，尝试切换到下一个片段
            currentClipIndex++;
            if (currentClipIndex >= (int) mainTrack.clips.size()) {
                // 所有片段播放完毕
                videoQueue->eof.store(true);
                audioQueue->eof.store(true);
                videoQueue->cond.notify_all();
                audioQueue->cond.notify_all();
                LOGD("All clips finished");
                break;
            }

            // 关闭当前片段，打开下一个
            LOGI("Switching to clip %d/%d", currentClipIndex + 1, (int) mainTrack.clips.size());
            source->close();
            delete source;

            source = new FFMediaSource();
            ret = source->open(mainTrack.clips[currentClipIndex], nativeWindow);
            if (ret < 0) {
                LOGE("Failed to open clip %d", currentClipIndex);
                delete source;
                source = nullptr;
                currentSource_ = nullptr;
                videoQueue->eof.store(true);
                audioQueue->eof.store(true);
                videoQueue->cond.notify_all();
                audioQueue->cond.notify_all();
                break;
            }
            currentSource_ = source;

            // 通知片段切换
            if (onClipSwitch) {
                onClipSwitch(currentClipIndex, source);
            }
            continue;
        }

        if (ret < 0) {
            usleep(5000);
            continue;
        }
    }

    // 清理
    if (source) {
        source->close();
        delete source;
        currentSource_ = nullptr;
    }

    LOGD("Read thread exited");
}