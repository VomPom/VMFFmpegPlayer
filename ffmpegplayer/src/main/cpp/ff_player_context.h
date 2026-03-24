#ifndef FFMPEG_PLAYER_FF_PLAYER_CONTEXT_H
#define FFMPEG_PLAYER_FF_PLAYER_CONTEXT_H

#include <jni.h>
#include <string>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <android/native_window.h>
#include <android/native_window_jni.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>
}

#include "ff_demuxer.h"
#include "ff_video_decoder.h"
#include "ff_audio_decoder.h"
#include "ff_av_sync.h"

/**
 * Player State Enum
 */
enum class FFPlayerState {
    IDLE = 0,
    PREPARING = 1,
    PREPARED = 2,
    PLAYING = 3,
    PAUSED = 4,
    STOPPED = 5,
    COMPLETED = 6,
    ERROR = 7
};

/**
 * FFPlayerContext - Player Core Context
 *
 * Complete playback flow:
 *   FFDemuxer(demux) --> FFVideoDecoder(MediaCodec H.264 HW decode → ANativeWindow)
 *                   --> FFAudioDecoder(FFmpeg SW decode → OpenSL ES playback)
 *                   --> FFAVSync(sync video frames based on audio clock)
 */
class FFPlayerContext {
public:
    FFPlayerContext(JavaVM *javaVM, jobject javaPlayer);

    ~FFPlayerContext();

    // Playback control
    int prepare(const std::string &path);

    int prepareWithFd(int fd);

    void start();

    void pause();

    void resume();

    void stop();

    void seekTo(int64_t positionMs);

    void release();

    void reset(); // Reset player state for replay

    // Set Surface
    void setSurface(JNIEnv *env, jobject surface);

    // Info query
    int64_t getDuration();

    int64_t getCurrentPosition();

    int getState();

    int getVideoWidth();

    int getVideoHeight();

private:
    // Java VM and callback object
    JavaVM *javaVM_ = nullptr;
    jobject javaPlayer_ = nullptr;  // GlobalRef

    // Sub-modules
    FFDemuxer *demuxer_ = nullptr;
    FFVideoDecoder *videoDecoder_ = nullptr;
    FFAudioDecoder *audioDecoder_ = nullptr;
    FFAVSync *avSync_ = nullptr;

    // Worker threads
    std::thread readThread_;
    std::thread videoThread_;
    std::thread audioThread_;

    // State control
    std::atomic<FFPlayerState> state_{FFPlayerState::IDLE};
    std::atomic<bool> abortRequest_{false};
    std::atomic<bool> seekRequest_{false};
    std::atomic<int64_t> seekPositionUs_{0};
    std::atomic<bool> seekDoneWhilePaused_{false}; // 暂停期间完成 seek，通知视频线程渲染一帧
    std::atomic<int64_t> seekTargetUs_{-1}; // 精确 seek 目标时间，>= 0 表示需要丢弃 PTS < 此值的帧

    // Packet queue (simplified: using vector + mutex)
    struct PacketQueue {
        std::vector<AVPacket *> packets;
        std::mutex mutex;
        std::condition_variable cond;
        std::atomic<bool> eof{false};
        std::atomic<bool> aborted{false};
        int maxSize = 128;

        void push(AVPacket *pkt) {
            std::unique_lock<std::mutex> lock(mutex);
            while ((int) packets.size() >= maxSize && !aborted.load()) {
                cond.wait_for(lock, std::chrono::milliseconds(10));
            }
            if (aborted.load()) {
                av_packet_free(&pkt);
                return;
            }
            packets.push_back(pkt);
            cond.notify_one();
        }

        AVPacket *pop() {
            std::unique_lock<std::mutex> lock(mutex);
            // Wait until data available, EOF, or abort
            while (packets.empty() && !eof.load() && !aborted.load()) {
                cond.wait_for(lock, std::chrono::milliseconds(10));
            }
            if (packets.empty()) return nullptr;
            AVPacket *pkt = packets.front();
            packets.erase(packets.begin());
            cond.notify_one();
            return pkt;
        }

        void flush() {
            std::lock_guard<std::mutex> lock(mutex);
            for (auto *pkt: packets) {
                av_packet_free(&pkt);
            }
            packets.clear();
            eof.store(false);
            aborted.store(false);
            cond.notify_all();
        }

        int size() {
            std::lock_guard<std::mutex> lock(mutex);
            return (int) packets.size();
        }

        void abort() {
            aborted.store(true);
            eof.store(true);
            cond.notify_all();
        }
    };

    PacketQueue videoQueue_;
    PacketQueue audioQueue_;

    // Surface
    ANativeWindow *nativeWindow_ = nullptr;
    std::mutex surfaceMutex_;

    // H.264/HEVC AVCC → AnnexB bitstream filter
    AVBSFContext *bsfCtx_ = nullptr;

    // Thread functions
    void readThreadFunc();

    void videoThreadFunc();

    void audioThreadFunc();

    // Java callbacks
    void notifyPrepared();

    void notifyCompletion();

    void notifyError(int code, const std::string &msg);

    void notifyProgress(int64_t currentMs, int64_t totalMs);

    void notifyVideoSizeChanged(int width, int height);

    void notifyStateChanged(int state);

    JNIEnv *getJNIEnv();

    void detachThread();
};

#endif // FFMPEG_PLAYER_FF_PLAYER_CONTEXT_H