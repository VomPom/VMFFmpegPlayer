#ifndef FFMPEG_PLAYER_FF_AUDIO_DECODER_H
#define FFMPEG_PLAYER_FF_AUDIO_DECODER_H

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>

/**
 * Audio Decoder + Player
 * FFmpeg software decoding + OpenSL ES playback
 *
 * Flow: AVPacket → FFmpeg decode → SwrResample(to PCM S16) → OpenSL ES playback
 */
class FFAudioDecoder {
public:
    FFAudioDecoder();
    ~FFAudioDecoder();

    /**
     * Initialize audio decoder and player
     * @param codecParams FFmpeg audio codec parameters
     * @return 0 on success
     */
    int init(AVCodecParameters *codecParams);

    /**
     * Send packet to decoder
     * @param packet Audio data packet (nullptr means EOS)
     * @return 0 on success
     */
    int sendPacket(AVPacket *packet);

    /**
     * Decode and push to playback buffer queue
     * @return 0 on frame output, negative on error/EOS
     */
    int decodeFrame();

    /** Get current audio playback clock (microseconds) */
    int64_t getAudioClockUs() const { return audioClockUs.load(); }

    /** Pause */
    void pause();

    /** Resume playback */
    void resume();

    /** Flush (call after seek) */
    void flush();

    /** Release resources */
    void release();

    bool isEOS() const { return eos.load(); }

private:
    // FFmpeg decoding
    AVCodecContext *codecCtx = nullptr;
    SwrContext *swrCtx = nullptr;

    // OpenSL ES playback
    SLObjectItf engineObj = nullptr;
    SLEngineItf engineItf = nullptr;
    SLObjectItf outputMixObj = nullptr;
    SLObjectItf playerObj = nullptr;
    SLPlayItf playItf = nullptr;
    SLAndroidSimpleBufferQueueItf bufferQueueItf = nullptr;

    // PCM buffer queue
    struct AudioBuffer {
        std::vector<uint8_t> data;
        int64_t ptsUs = 0;
    };
    std::queue<AudioBuffer> bufferQueue;
    std::mutex queueMutex;
    std::condition_variable queueCond;

    // Currently playing buffer
    std::vector<uint8_t> playingBuffer;

    std::atomic<int64_t> audioClockUs{0};
    std::atomic<bool> eos{false};

    int outSampleRate = 44100;
    int outChannels = 2;

    void initOpenSLES();
    static void bufferQueueCallback(SLAndroidSimpleBufferQueueItf bq, void *context);
    void onBufferQueueCallback();
};

#endif // FFMPEG_PLAYER_FF_AUDIO_DECODER_H
