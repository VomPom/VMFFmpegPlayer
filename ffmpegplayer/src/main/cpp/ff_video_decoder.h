#ifndef FFMPEG_PLAYER_FF_VIDEO_DECODER_H
#define FFMPEG_PLAYER_FF_VIDEO_DECODER_H

#include <jni.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <android/native_window.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

/**
 * H.264 Hardware Video Decoder
 * Uses FFmpeg demuxing + Android MediaCodec (NDK) for hardware decoding
 *
 * Flow: AVPacket(H.264) → MediaCodec HW decode → ANativeWindow output
 */
class FFVideoDecoder {
public:
    FFVideoDecoder();
    ~FFVideoDecoder();

    /**
     * Initialize decoder
     * @param codecParams FFmpeg codec parameters (with extradata/SPS/PPS)
     * @param window ANativeWindow output window
     * @return 0 on success
     */
    int init(AVCodecParameters *codecParams, ANativeWindow *window);

    /**
     * Send packet to decoder
     * @param packet H.264 data packet (nullptr means EOS)
     * @return 0 on success, 1 need retry (decoder full)
     */
    int sendPacket(AVPacket *packet);

    /**
     * Receive decoded frame and render to Surface
     * @param ptsUs Output frame PTS (microseconds)
     * @return 0 frame output, 1 no frame, negative error/EOS
     */
    int receiveFrame(int64_t &ptsUs);

    /**
     * Receive decoded frame with render control
     * @param ptsUs Output frame PTS (microseconds)
     * @param render true=render to Surface, false=discard frame
     * @return 0 frame output, 1 no frame, negative error/EOS
     */
    int receiveFrame(int64_t &ptsUs, bool render);

    /**
     * Dequeue frame from decoder (does not release buffer), get PTS and buffer index
     * Must call releaseFrame() afterwards to release buffer
     * @param ptsUs Output frame PTS (microseconds)
     * @param outBufIdx Output buffer index
     * @return 0 frame output, 1 no frame, negative error/EOS
     */
    int dequeueFrame(int64_t &ptsUs, ssize_t &outBufIdx);

    /**
     * Release decoder output buffer
     * @param bufIdx Buffer index (from dequeueFrame)
     * @param render true=render to Surface, false=discard frame
     */
    void releaseFrame(ssize_t bufIdx, bool render);

    /** Flush decoder (call after seek) */
    void flush();

    /** Release resources */
    void release();

    bool isEOS() const { return eos.load(); }

private:
    AMediaCodec *mediaCodec = nullptr;
    ANativeWindow *nativeWindow = nullptr;
    std::atomic<bool> eos{false};

    /**
     * Extract H.264 SPS/PPS from AVCodecParameters to construct csd-0 / csd-1
     */
    void extractCsd(AVCodecParameters *params, AMediaFormat *format);
};

#endif // FFMPEG_PLAYER_FF_VIDEO_DECODER_H
