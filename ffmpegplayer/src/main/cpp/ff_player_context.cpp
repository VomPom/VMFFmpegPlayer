#include "ff_player_context.h"
#include <android/log.h>
#include <unistd.h>

#define LOG_TAG "FFPlayerContext"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

FFPlayerContext::FFPlayerContext(JavaVM *javaVM, jobject javaPlayer)
        : javaVM_(javaVM) {
    JNIEnv *env = getJNIEnv();
    if (env) {
        javaPlayer_ = env->NewGlobalRef(javaPlayer);
    }
}

FFPlayerContext::~FFPlayerContext() {
    release();
}

JNIEnv *FFPlayerContext::getJNIEnv() {
    JNIEnv *env = nullptr;
    if (javaVM_) {
        int status = javaVM_->GetEnv((void **) &env, JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            javaVM_->AttachCurrentThread(&env, nullptr);
        }
    }
    return env;
}

void FFPlayerContext::detachThread() {
    if (javaVM_) {
        javaVM_->DetachCurrentThread();
    }
}

// ==================== Playback Control ====================

int FFPlayerContext::prepare(const std::string &path) {
    FFPlayerState curState = state_.load();
    if (curState != FFPlayerState::IDLE && curState != FFPlayerState::STOPPED) {
        LOGE("prepare: invalid state: %d", (int) curState);
        return -1;
    }

    // If re-preparing from STOPPED state, release old resources first
    if (curState == FFPlayerState::STOPPED) {
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
        if (avSync_) {
            delete avSync_;
            avSync_ = nullptr;
        }
        // 释放特效管线（重新 prepare 时会重建）
        if (effectPipeline_) {
            delete effectPipeline_;
            effectPipeline_ = nullptr;
        }
        speedEffect_.reset();
        if (bsfCtx_) {
            av_bsf_free(&bsfCtx_);
            bsfCtx_ = nullptr;
        }
    }

    state_.store(FFPlayerState::PREPARING);
    notifyStateChanged((int) FFPlayerState::PREPARING);

    // Create demuxer
    demuxer_ = new FFDemuxer();
    int ret = demuxer_->open(path.c_str());
    if (ret < 0) {
        LOGE("Failed to open file: %s", path.c_str());
        notifyError(-1, "Failed to open file");
        state_.store(FFPlayerState::ERROR);
        return ret;
    }

    // Create AV sync
    avSync_ = new FFAVSync();

    // 初始化特效管线
    if (!effectPipeline_) {
        effectPipeline_ = new EffectPipeline();
        // 注册内置变速特效
        speedEffect_ = std::make_shared<SpeedEffect>();
        effectPipeline_->addEffect(speedEffect_);
        LOGD("特效管线已初始化，内置变速特效已注册");
    }

    // Create video decoder
    {
        std::lock_guard<std::mutex> lock(surfaceMutex_);
        if (nativeWindow_ && demuxer_->getVideoStreamIndex() >= 0) {
            videoDecoder_ = new FFVideoDecoder();
            ret = videoDecoder_->init(demuxer_->getVideoCodecParams(), nativeWindow_);
            if (ret < 0) {
                LOGE("Video decoder init failed");
                delete videoDecoder_;
                videoDecoder_ = nullptr;
            }
        }
    }

    // Init H.264/HEVC bitstream filter (AVCC → AnnexB), MediaCodec requires AnnexB format
    if (demuxer_->getVideoStreamIndex() >= 0) {
        AVCodecParameters *vpar = demuxer_->getVideoCodecParams();
        const char *bsfName = nullptr;
        if (vpar->codec_id == AV_CODEC_ID_H264) {
            bsfName = "h264_mp4toannexb";
        } else if (vpar->codec_id == AV_CODEC_ID_HEVC) {
            bsfName = "hevc_mp4toannexb";
        }
        if (bsfName) {
            const AVBitStreamFilter *bsf = av_bsf_get_by_name(bsfName);
            if (bsf) {
                ret = av_bsf_alloc(bsf, &bsfCtx_);
                if (ret >= 0) {
                    avcodec_parameters_copy(bsfCtx_->par_in, vpar);
                    bsfCtx_->time_base_in = demuxer_->getVideoTimeBase();
                    ret = av_bsf_init(bsfCtx_);
                    if (ret < 0) {
                        LOGE("Bitstream filter init failed: %d", ret);
                        av_bsf_free(&bsfCtx_);
                        bsfCtx_ = nullptr;
                    } else {
                        LOGD("Bitstream filter [%s] initialized successfully", bsfName);
                    }
                }
            } else {
                LOGE("Bitstream filter not found: %s", bsfName);
            }
        }
    }

    // Create audio decoder
    if (demuxer_->getAudioStreamIndex() >= 0) {
        audioDecoder_ = new FFAudioDecoder();
        ret = audioDecoder_->init(demuxer_->getAudioCodecParams());
        if (ret < 0) {
            LOGE("Audio decoder init failed");
            delete audioDecoder_;
            audioDecoder_ = nullptr;
        }
    }

    state_.store(FFPlayerState::PREPARED);
    notifyStateChanged((int) FFPlayerState::PREPARED);
    notifyPrepared();

    if (videoDecoder_) {
        notifyVideoSizeChanged(demuxer_->getWidth(), demuxer_->getHeight());
    }

    LOGI("Prepare done: duration=%lldms, video=%dx%d, fps=%.1f",
         (long long) (demuxer_->getDurationUs() / 1000),
         demuxer_->getWidth(), demuxer_->getHeight(),
         demuxer_->getVideoFps());

    return 0;
}

int FFPlayerContext::prepareWithFd(int fd) {
    FFPlayerState curState = state_.load();
    if (curState != FFPlayerState::IDLE && curState != FFPlayerState::STOPPED) {
        return -1;
    }

    // If re-preparing from STOPPED state, release old resources first
    if (curState == FFPlayerState::STOPPED) {
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
        if (avSync_) {
            delete avSync_;
            avSync_ = nullptr;
        }
        // 释放特效管线（重新 prepare 时会重建）
        if (effectPipeline_) {
            delete effectPipeline_;
            effectPipeline_ = nullptr;
        }
        speedEffect_.reset();
        if (bsfCtx_) {
            av_bsf_free(&bsfCtx_);
            bsfCtx_ = nullptr;
        }
    }

    state_.store(FFPlayerState::PREPARING);
    notifyStateChanged((int) FFPlayerState::PREPARING);

    demuxer_ = new FFDemuxer();
    int ret = demuxer_->openFd(fd);
    if (ret < 0) {
        notifyError(-1, "Failed to open file via fd");
        state_.store(FFPlayerState::ERROR);
        return ret;
    }

    avSync_ = new FFAVSync();

    // 初始化特效管线
    if (!effectPipeline_) {
        effectPipeline_ = new EffectPipeline();
        speedEffect_ = std::make_shared<SpeedEffect>();
        effectPipeline_->addEffect(speedEffect_);
    }

    {
        std::lock_guard<std::mutex> lock(surfaceMutex_);
        if (nativeWindow_ && demuxer_->getVideoStreamIndex() >= 0) {
            videoDecoder_ = new FFVideoDecoder();
            ret = videoDecoder_->init(demuxer_->getVideoCodecParams(), nativeWindow_);
            if (ret < 0) {
                delete videoDecoder_;
                videoDecoder_ = nullptr;
            }
        }
    }

    // Init bitstream filter (same as prepare method)
    if (demuxer_->getVideoStreamIndex() >= 0) {
        AVCodecParameters *vpar = demuxer_->getVideoCodecParams();
        const char *bsfName = nullptr;
        if (vpar->codec_id == AV_CODEC_ID_H264) {
            bsfName = "h264_mp4toannexb";
        } else if (vpar->codec_id == AV_CODEC_ID_HEVC) {
            bsfName = "hevc_mp4toannexb";
        }
        if (bsfName) {
            const AVBitStreamFilter *bsf = av_bsf_get_by_name(bsfName);
            if (bsf) {
                ret = av_bsf_alloc(bsf, &bsfCtx_);
                if (ret >= 0) {
                    avcodec_parameters_copy(bsfCtx_->par_in, vpar);
                    bsfCtx_->time_base_in = demuxer_->getVideoTimeBase();
                    ret = av_bsf_init(bsfCtx_);
                    if (ret < 0) {
                        av_bsf_free(&bsfCtx_);
                        bsfCtx_ = nullptr;
                    }
                }
            }
        }
    }

    if (demuxer_->getAudioStreamIndex() >= 0) {
        audioDecoder_ = new FFAudioDecoder();
        ret = audioDecoder_->init(demuxer_->getAudioCodecParams());
        if (ret < 0) {
            delete audioDecoder_;
            audioDecoder_ = nullptr;
        }
    }

    state_.store(FFPlayerState::PREPARED);
    notifyStateChanged((int) FFPlayerState::PREPARED);
    notifyPrepared();

    if (videoDecoder_) {
        notifyVideoSizeChanged(demuxer_->getWidth(), demuxer_->getHeight());
    }

    return 0;
}

void FFPlayerContext::start() {
    FFPlayerState expected = FFPlayerState::PREPARED;
    if (!state_.compare_exchange_strong(expected, FFPlayerState::PLAYING)) {
        // Also allow starting from PAUSED state
        expected = FFPlayerState::PAUSED;
        if (!state_.compare_exchange_strong(expected, FFPlayerState::PLAYING)) {
            LOGE("start: invalid state %d", (int) state_.load());
            return;
        }
        // Resume from pause
        resume();
        return;
    }

    abortRequest_.store(false);
    notifyStateChanged((int) FFPlayerState::PLAYING);

    // Start read thread
    readThread_ = std::thread(&FFPlayerContext::readThreadFunc, this);

    // Start video decode thread
    if (videoDecoder_) {
        videoThread_ = std::thread(&FFPlayerContext::videoThreadFunc, this);
    }

    // Start audio decode thread
    if (audioDecoder_) {
        audioThread_ = std::thread(&FFPlayerContext::audioThreadFunc, this);
    }

    LOGI("Playback started");
}

void FFPlayerContext::pause() {
    FFPlayerState expected = FFPlayerState::PLAYING;
    if (!state_.compare_exchange_strong(expected, FFPlayerState::PAUSED)) {
        return;
    }

    if (audioDecoder_) {
        audioDecoder_->pause();
    }

    notifyStateChanged((int) FFPlayerState::PAUSED);
    LOGI("Playback paused");
}

void FFPlayerContext::resume() {
    FFPlayerState expected = FFPlayerState::PAUSED;
    if (!state_.compare_exchange_strong(expected, FFPlayerState::PLAYING)) {
        return;
    }

    if (audioDecoder_) {
        audioDecoder_->resume();
    }

    notifyStateChanged((int) FFPlayerState::PLAYING);
    LOGI("Playback resumed");
}

void FFPlayerContext::stop() {
    FFPlayerState curState = state_.load();
    if (curState == FFPlayerState::STOPPED || curState == FFPlayerState::IDLE) {
        return;
    }

    LOGI("Stopping playback, current state: %d", (int) curState);

    // Set abort flag
    abortRequest_.store(true);

    // 中断 demuxer 的网络 IO，避免阻塞在网络读取上
    if (demuxer_) {
        demuxer_->abort();
    }

    // Notify queues to stop
    videoQueue_.abort();
    audioQueue_.abort();

    // Wait for threads to finish
    auto joinThread = [](std::thread &thread, const char *threadName) {
        if (thread.joinable()) {
            LOGI("Waiting for %s thread to finish", threadName);
            thread.join();
            LOGI("%s thread finished", threadName);
        }
    };

    joinThread(readThread_, "read");
    joinThread(videoThread_, "video");
    joinThread(audioThread_, "audio");

    // Flush queues
    videoQueue_.flush();
    audioQueue_.flush();

    state_.store(FFPlayerState::STOPPED);
    notifyStateChanged((int) FFPlayerState::STOPPED);
    LOGI("Playback stopped");
}

void FFPlayerContext::seekTo(int64_t positionMs) {
    int64_t targetUs = positionMs * 1000;
    seekPositionUs_.store(targetUs);
    seekTargetUs_.store(targetUs); // 设置精确 seek 目标，视频线程会丢弃 PTS < 此值的帧
    seekRequest_.store(true);
    LOGI("Seek to %lld ms (target %lld us)", (long long) positionMs, (long long) targetUs);
}

void FFPlayerContext::reset() {
    LOGI("Resetting player state");

    // Stop current playback (waits for all threads to exit)
    stop();

    // Reset all state variables
    abortRequest_.store(false);
    seekRequest_.store(false);
    seekPositionUs_.store(0);
    seekDoneWhilePaused_.store(false);
    seekTargetUs_.store(-1);

    // Flush and reset queue state
    videoQueue_.flush();
    audioQueue_.flush();

    // Seek demuxer to beginning
    if (demuxer_) {
        demuxer_->seek(0);
    }

    // Reset video decoder (flush MediaCodec and reset EOS state)
    if (videoDecoder_) {
        videoDecoder_->flush();
    }

    // Reset audio decoder (flush FFmpeg decoder, clear OpenSL ES buffer queue, reset clock)
    if (audioDecoder_) {
        audioDecoder_->flush();
    }

    // Reset sync manager
    if (avSync_) {
        avSync_->reset();
    }

    // 重置特效管线状态
    if (effectPipeline_) {
        effectPipeline_->resetAll();
    }

    // Reset state to PREPARED so start() can be called directly
    state_.store(FFPlayerState::PREPARED);
    notifyStateChanged((int) FFPlayerState::PREPARED);

    LOGI("Player reset complete, state set to PREPARED");
}

void FFPlayerContext::release() {
    if (state_.load() == FFPlayerState::PLAYING || state_.load() == FFPlayerState::PAUSED) {
        stop();
    }

    // 确保 demuxer 网络 IO 已中断
    if (demuxer_) {
        demuxer_->abort();
    }

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

    if (avSync_) {
        delete avSync_;
        avSync_ = nullptr;
    }

    // 释放特效管线
    if (effectPipeline_) {
        delete effectPipeline_;
        effectPipeline_ = nullptr;
    }
    speedEffect_.reset();

    if (bsfCtx_) {
        av_bsf_free(&bsfCtx_);
        bsfCtx_ = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(surfaceMutex_);
        if (nativeWindow_) {
            ANativeWindow_release(nativeWindow_);
            nativeWindow_ = nullptr;
        }
    }

    // Release Java global reference
    JNIEnv *env = getJNIEnv();
    if (env && javaPlayer_) {
        env->DeleteGlobalRef(javaPlayer_);
        javaPlayer_ = nullptr;
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

        // If prepare was already called, need to reinit video decoder
        if (state_.load() >= FFPlayerState::PREPARED && demuxer_ && demuxer_->getVideoStreamIndex() >= 0) {
            if (videoDecoder_) {
                videoDecoder_->release();
                delete videoDecoder_;
                videoDecoder_ = nullptr;
            }

            if (nativeWindow_) {
                videoDecoder_ = new FFVideoDecoder();
                int ret = videoDecoder_->init(demuxer_->getVideoCodecParams(), nativeWindow_);
                if (ret < 0) {
                    LOGE("Video decoder reinit failed");
                    delete videoDecoder_;
                    videoDecoder_ = nullptr;
                } else {
                    LOGD("Video decoder reinitialized successfully");
                }
            }
        }
    } else {
        LOGD("Surface cleared");
    }
}

// ==================== Info Query ====================

int64_t FFPlayerContext::getDuration() {
    if (demuxer_) {
        return demuxer_->getDurationUs() / 1000; // Return milliseconds
    }
    return 0;
}

int64_t FFPlayerContext::getCurrentPosition() {
    if (audioDecoder_) {
        return audioDecoder_->getAudioClockUs() / 1000; // Return milliseconds
    }
    return 0;
}

int FFPlayerContext::getState() {
    return (int) state_.load();
}

int FFPlayerContext::getVideoWidth() {
    return demuxer_ ? demuxer_->getWidth() : 0;
}

int FFPlayerContext::getVideoHeight() {
    return demuxer_ ? demuxer_->getHeight() : 0;
}

// ==================== Thread Functions ====================

void FFPlayerContext::readThreadFunc() {
    LOGD("Read thread started");

    int videoIdx = demuxer_->getVideoStreamIndex();
    int audioIdx = demuxer_->getAudioStreamIndex();

    // Get time_base for each stream for PTS conversion to microseconds
    AVRational videoTb = demuxer_->getVideoTimeBase();
    AVRational audioTb = demuxer_->getAudioTimeBase();
    AVRational usTb = {1, AV_TIME_BASE}; // microsecond time_base
    LOGD("Video time_base: %d/%d, Audio time_base: %d/%d",
         videoTb.num, videoTb.den, audioTb.num, audioTb.den);

    while (!abortRequest_.load()) {
        // Handle seek request (must be before pause check so seek works while paused)
        if (seekRequest_.load()) {
            int64_t seekPos = seekPositionUs_.load();
            bool wasPaused = (state_.load() == FFPlayerState::PAUSED);
            int ret = demuxer_->seek(seekPos);
            if (ret >= 0) {
                // Flush queues
                videoQueue_.flush();
                audioQueue_.flush();
                // Flush decoders
                if (videoDecoder_) videoDecoder_->flush();
                if (audioDecoder_) audioDecoder_->flush();

                // 暂停状态下 seek 后，预读几个视频包送入队列，以便视频线程解码渲染一帧
                if (wasPaused && videoDecoder_) {
                    int videoIdx = demuxer_->getVideoStreamIndex();
                    AVRational seekVideoTb = demuxer_->getVideoTimeBase();
                    AVRational seekUsTb = {1, AV_TIME_BASE};
                    int prereadCount = 0;
                    // 预读最多 300 个视频包，确保覆盖整个 GOP 间隔以精确定位到目标帧
                    while (prereadCount < 300) {
                        AVPacket *pkt = av_packet_alloc();
                        int readRet = demuxer_->readPacket(pkt);
                        if (readRet < 0) {
                            av_packet_free(&pkt);
                            break;
                        }
                        if (pkt->stream_index == videoIdx) {
                            // 通过 bsf 过滤并转换时间基
                            if (bsfCtx_) {
                                int bsfRet = av_bsf_send_packet(bsfCtx_, pkt);
                                if (bsfRet >= 0) {
                                    while (av_bsf_receive_packet(bsfCtx_, pkt) == 0) {
                                        if (pkt->pts != AV_NOPTS_VALUE)
                                            pkt->pts = av_rescale_q(pkt->pts, seekVideoTb, seekUsTb);
                                        if (pkt->dts != AV_NOPTS_VALUE)
                                            pkt->dts = av_rescale_q(pkt->dts, seekVideoTb, seekUsTb);
                                        videoQueue_.push(pkt);
                                        prereadCount++;
                                        pkt = av_packet_alloc();
                                    }
                                }
                                av_packet_free(&pkt);
                            } else {
                                if (pkt->pts != AV_NOPTS_VALUE)
                                    pkt->pts = av_rescale_q(pkt->pts, seekVideoTb, seekUsTb);
                                if (pkt->dts != AV_NOPTS_VALUE)
                                    pkt->dts = av_rescale_q(pkt->dts, seekVideoTb, seekUsTb);
                                videoQueue_.push(pkt);
                                prereadCount++;
                            }
                        } else {
                            av_packet_free(&pkt);
                        }
                    }
                    seekDoneWhilePaused_.store(true);
                    LOGD("Paused seek: preread %d video packets", prereadCount);
                }
            }
            seekRequest_.store(false);
        }

        // Check pause state
        if (state_.load() == FFPlayerState::PAUSED) {
            usleep(10000); // 10ms
            continue;
        }

        // Control queue size to avoid excessive memory usage
        if (videoQueue_.size() > 64 || audioQueue_.size() > 64) {
            usleep(10000);
            continue;
        }

        AVPacket *packet = av_packet_alloc();
        int ret = demuxer_->readPacket(packet);

        if (ret == AVERROR_EOF) {
            av_packet_free(&packet);
            // Send EOF marker
            videoQueue_.eof.store(true);
            audioQueue_.eof.store(true);
            videoQueue_.cond.notify_all();
            audioQueue_.cond.notify_all();
            LOGD("Reached end of file");
            break;
        }

        if (ret < 0) {
            av_packet_free(&packet);
            usleep(5000);
            continue;
        }

        // Dispatch packet by stream index and convert PTS to microseconds
        if (packet->stream_index == videoIdx) {
            // Convert AVCC to AnnexB via bitstream filter (required by MediaCodec)
            if (bsfCtx_) {
                int bsfRet = av_bsf_send_packet(bsfCtx_, packet);
                if (bsfRet < 0) {
                    av_packet_free(&packet);
                    continue;
                }
                while (av_bsf_receive_packet(bsfCtx_, packet) == 0) {
                    if (packet->pts != AV_NOPTS_VALUE) {
                        packet->pts = av_rescale_q(packet->pts, videoTb, usTb);
                    }
                    if (packet->dts != AV_NOPTS_VALUE) {
                        packet->dts = av_rescale_q(packet->dts, videoTb, usTb);
                    }
                    videoQueue_.push(packet);
                    packet = av_packet_alloc(); // Allocate new packet for next receive
                }
                av_packet_free(&packet); // Free last unused packet
            } else {
                // No bitstream filter needed (non H.264/HEVC or VP8/VP9)
                if (packet->pts != AV_NOPTS_VALUE) {
                    packet->pts = av_rescale_q(packet->pts, videoTb, usTb);
                }
                if (packet->dts != AV_NOPTS_VALUE) {
                    packet->dts = av_rescale_q(packet->dts, videoTb, usTb);
                }
                videoQueue_.push(packet);
            }
            // Packet already pushed or freed, no further handling needed
            continue;
        } else if (packet->stream_index == audioIdx) {
            if (packet->pts != AV_NOPTS_VALUE) {
                packet->pts = av_rescale_q(packet->pts, audioTb, usTb);
            }
            if (packet->dts != AV_NOPTS_VALUE) {
                packet->dts = av_rescale_q(packet->dts, audioTb, usTb);
            }
            audioQueue_.push(packet);
        } else {
            av_packet_free(&packet);
        }
    }

    LOGD("Read thread exited");
}

void FFPlayerContext::videoThreadFunc() {
    LOGD("Video thread started");

    // Variables for frame rate control
    int64_t lastPtsUs = 0;

    while (!abortRequest_.load()) {
        if (state_.load() == FFPlayerState::PAUSED) {
            // 暂停状态下，如果刚完成 seek，解码并渲染一帧以实现拖动预览
            if (seekDoneWhilePaused_.load()) {
                seekDoneWhilePaused_.store(false);
                // 从队列中取包、解码、精确定位到目标帧并渲染
                int64_t pauseSeekTarget = seekTargetUs_.load();
                bool rendered = false;
                while (!abortRequest_.load()) {
                    AVPacket *pkt = videoQueue_.pop();
                    if (!pkt) break;
                    int sendRet = videoDecoder_->sendPacket(pkt);
                    av_packet_free(&pkt);
                    if (sendRet != 0) continue;

                    // 尝试取出解码帧
                    while (!abortRequest_.load()) {
                        int64_t framePts = 0;
                        ssize_t frameBufIdx = -1;
                        int recvRet = videoDecoder_->dequeueFrame(framePts, frameBufIdx);
                        if (recvRet != 0 || frameBufIdx < 0) break;

                        // 精确 seek：PTS < 目标的帧只解码不渲染
                        if (pauseSeekTarget >= 0 && framePts < pauseSeekTarget) {
                            videoDecoder_->releaseFrame(frameBufIdx, false); // 丢弃
                            continue;
                        }

                        // PTS >= 目标，渲染这一帧
                        videoDecoder_->releaseFrame(frameBufIdx, true);
                        rendered = true;
                        seekTargetUs_.store(-1);
                        break;
                    }
                    if (rendered) break;
                }
                // 清空预读队列中剩余的包
                while (true) {
                    AVPacket *remaining = videoQueue_.pop();
                    if (!remaining) break;
                    av_packet_free(&remaining);
                }
            }
            usleep(10000);
            continue;
        }

        // Pop packet from queue
        AVPacket *packet = videoQueue_.pop();
        if (!packet) {
            if (videoQueue_.eof.load() && videoQueue_.size() == 0) {
                videoDecoder_->sendPacket(nullptr);
                // Drain decoder
                while (!abortRequest_.load()) {
                    int64_t ptsUs = 0;
                    int ret = videoDecoder_->receiveFrame(ptsUs, false); // Don't render to Surface
                    if (ret < 0) break;
                }
                break;
            }
            usleep(2000);
            continue;
        }

        // Send packet to decoder
        int sendRet = videoDecoder_->sendPacket(packet);
        av_packet_free(&packet);

        if (sendRet != 0) {
            // Decoder full, retry later
            usleep(1000);
            continue;
        }

        // Receive decoded frames, do AV sync then render
        while (!abortRequest_.load()) {
            int64_t ptsUs = 0;
            ssize_t bufIdx = -1;
            int recvRet = videoDecoder_->dequeueFrame(ptsUs, bufIdx);

            if (recvRet != 0) {
                // 1 = no frame available (EAGAIN), break inner loop to send next packet
                // < 0 = EOS or error
                break;
            }

            if (bufIdx < 0) break; // Defensive check

            // 精确 seek：丢弃 PTS < seek 目标的帧（只解码不渲染）
            int64_t curSeekTarget = seekTargetUs_.load();
            if (curSeekTarget >= 0 && ptsUs < curSeekTarget) {
                videoDecoder_->releaseFrame(bufIdx, false); // 丢弃这一帧
                continue; // 继续解码下一帧
            }
            // 已达到或超过目标位置，清除精确 seek 标志
            if (curSeekTarget >= 0) {
                seekTargetUs_.store(-1);
            }

            // ==================== 特效管线处理 ====================
            // 通过特效管线处理视频帧时间轴（变速等时间类特效在此生效）
            int64_t effectivePtsUs = ptsUs;
            float currentSpeed = 1.0f;
            if (effectPipeline_) {
                TimeEffectResult timeResult;
                effectPipeline_->processVideoTime(ptsUs, timeResult);
                effectivePtsUs = timeResult.adjustedPtsUs;
                currentSpeed = timeResult.speedFactor;

                // 特效要求丢弃该帧
                if (timeResult.shouldDrop) {
                    videoDecoder_->releaseFrame(bufIdx, false);
                    continue;
                }
            }

            // AV sync decision（使用特效处理后的 PTS）
            bool shouldRender = true;
            if (avSync_ && audioDecoder_) {
                int64_t audioClk = audioDecoder_->getAudioClockUs();
                if (audioClk > 0) {
                    // 变速时需要将音频时钟也映射到调整后的时间轴
                    // 音频时钟反映的是原始时间轴上的位置，需要除以速度因子
                    int64_t adjustedAudioClk = audioClk;
                    if (currentSpeed != 1.0f && currentSpeed > 0.0f) {
                        // 音频实际播放速度也会变化，所以直接用原始 PTS 和原始音频时钟做同步
                        // 但帧间等待时间需要除以速度因子
                    }

                    int64_t waitUs = 0;
                    auto action = avSync_->sync(ptsUs, audioClk, waitUs);
                    if (action == FFAVSync::WAIT && waitUs > 0) {
                        // 变速时调整等待时间：快放缩短等待，慢放拉长等待
                        int64_t adjustedWaitUs = (int64_t) ((double) waitUs / (double) currentSpeed);
                        // Video ahead, wait before rendering
                        // Segmented sleep to respond to abort requests promptly
                        int64_t remaining = std::min(adjustedWaitUs, (int64_t) 100000);
                        while (remaining > 0 && !abortRequest_.load()) {
                            int64_t sleepTime = std::min(remaining, (int64_t) 5000);
                            usleep((useconds_t) sleepTime);
                            remaining -= sleepTime;
                        }
                        shouldRender = true; // Render after waiting
                    } else if (action == FFAVSync::DROP) {
                        // Video lagging too much, drop frame to catch up with audio
                        shouldRender = false;
                    }
                    // RENDER: diff within acceptable range, render directly
                } else {
                    // Audio clock not yet updated (just started), use PTS-based frame rate control
                    if (lastPtsUs > 0 && ptsUs > lastPtsUs) {
                        int64_t frameDuration = ptsUs - lastPtsUs;
                        // 变速时调整帧间隔
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
                // No audio stream, control playback speed based on frame rate
                static int64_t lastFrameTime = av_gettime();
                int64_t currentTime = av_gettime();
                int64_t frameDuration = ptsUs - lastPtsUs;

                // 变速时调整帧间隔
                if (currentSpeed != 1.0f && currentSpeed > 0.0f) {
                    frameDuration = (int64_t) ((double) frameDuration / (double) currentSpeed);
                }

                if (frameDuration > 0 && frameDuration < 1000000) { // Ensure reasonable frame interval (<1s)
                    int64_t elapsed = currentTime - lastFrameTime;
                    int64_t sleepTime = frameDuration - elapsed;

                    if (sleepTime > 0 && sleepTime < 100000) { // Reasonable wait time (<100ms)
                        usleep((useconds_t) sleepTime);
                    }
                }

                lastFrameTime = av_gettime();
                lastPtsUs = ptsUs;
            }

            // Release frame: render to Surface or discard
            videoDecoder_->releaseFrame(bufIdx, shouldRender);
        }
    }

    // Check if playback completed (not user-initiated stop)
    if (!abortRequest_.load() && videoDecoder_->isEOS()) {
        // Wait for audio to finish too
        while (!abortRequest_.load() && audioDecoder_ && !audioDecoder_->isEOS()) {
            usleep(10000);
        }
        state_.store(FFPlayerState::COMPLETED);
        notifyStateChanged((int) FFPlayerState::COMPLETED);
        notifyCompletion();
    }

    LOGD("Video thread exited");
}

void FFPlayerContext::audioThreadFunc() {
    LOGD("Audio thread started");

    while (!abortRequest_.load()) {
        if (state_.load() == FFPlayerState::PAUSED) {
            usleep(10000);
            continue;
        }

        // Pop packet from queue
        AVPacket *packet = audioQueue_.pop();
        if (!packet) {
            if (audioQueue_.eof.load() && audioQueue_.size() == 0) {
                audioDecoder_->sendPacket(nullptr);
                // Drain decoder
                while (!abortRequest_.load()) {
                    int ret = audioDecoder_->decodeFrame();
                    if (ret < 0) break;
                }
                break;
            }
            usleep(2000);
            continue;
        }

        // Send to decoder
        audioDecoder_->sendPacket(packet);
        av_packet_free(&packet);

        // Receive decoded frames and push to playback queue
        while (!abortRequest_.load()) {
            int ret = audioDecoder_->decodeFrame();
            if (ret != 0) break; // EAGAIN or EOF
        }
    }

    LOGD("Audio thread exited");
}

// ==================== 特效控制 ====================

void FFPlayerContext::setSpeed(float speed) {
    if (speedEffect_) {
        speedEffect_->setSpeed(speed);
        LOGI("播放速度设置为: %.2f", speed);
    }
}

float FFPlayerContext::getSpeed() {
    if (speedEffect_) {
        return speedEffect_->getSpeedFactor();
    }
    return 1.0f;
}

// ==================== Java Callbacks ====================

void FFPlayerContext::notifyPrepared() {
    JNIEnv *env = getJNIEnv();
    if (!env || !javaPlayer_) return;

    jclass clazz = env->GetObjectClass(javaPlayer_);
    jmethodID method = env->GetMethodID(clazz, "onNativePrepared", "(J)V");
    if (method) {
        env->CallVoidMethod(javaPlayer_, method, getDuration());
    }
    env->DeleteLocalRef(clazz);
}

void FFPlayerContext::notifyCompletion() {
    JNIEnv *env = getJNIEnv();
    if (!env || !javaPlayer_) return;

    jclass clazz = env->GetObjectClass(javaPlayer_);
    jmethodID method = env->GetMethodID(clazz, "onNativeCompletion", "()V");
    if (method) {
        env->CallVoidMethod(javaPlayer_, method);
    }
    env->DeleteLocalRef(clazz);
}

void FFPlayerContext::notifyError(int code, const std::string &msg) {
    JNIEnv *env = getJNIEnv();
    if (!env || !javaPlayer_) return;

    jclass clazz = env->GetObjectClass(javaPlayer_);
    jmethodID method = env->GetMethodID(clazz, "onNativeError", "(ILjava/lang/String;)V");
    if (method) {
        jstring jMsg = env->NewStringUTF(msg.c_str());
        env->CallVoidMethod(javaPlayer_, method, code, jMsg);
        env->DeleteLocalRef(jMsg);
    }
    env->DeleteLocalRef(clazz);
}

void FFPlayerContext::notifyProgress(int64_t currentMs, int64_t totalMs) {
    JNIEnv *env = getJNIEnv();
    if (!env || !javaPlayer_) return;

    jclass clazz = env->GetObjectClass(javaPlayer_);
    jmethodID method = env->GetMethodID(clazz, "onNativeProgress", "(JJ)V");
    if (method) {
        env->CallVoidMethod(javaPlayer_, method, currentMs, totalMs);
    }
    env->DeleteLocalRef(clazz);
}

void FFPlayerContext::notifyVideoSizeChanged(int width, int height) {
    JNIEnv *env = getJNIEnv();
    if (!env || !javaPlayer_) return;

    jclass clazz = env->GetObjectClass(javaPlayer_);
    jmethodID method = env->GetMethodID(clazz, "onNativeVideoSizeChanged", "(II)V");
    if (method) {
        env->CallVoidMethod(javaPlayer_, method, width, height);
    }
    env->DeleteLocalRef(clazz);
}

void FFPlayerContext::notifyStateChanged(int state) {
    JNIEnv *env = getJNIEnv();
    if (!env || !javaPlayer_) return;

    jclass clazz = env->GetObjectClass(javaPlayer_);
    jmethodID method = env->GetMethodID(clazz, "onNativeStateChanged", "(I)V");
    if (method) {
        env->CallVoidMethod(javaPlayer_, method, state);
    }
    env->DeleteLocalRef(clazz);
}