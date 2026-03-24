#include "ff_video_decoder.h"
#include <android/log.h>
#include <cstring>

#define LOG_TAG "FFVideoDecoder"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// MediaCodec timeout (microseconds)
static const int64_t DEQUEUE_TIMEOUT_US = 10000; // 10ms

FFVideoDecoder::FFVideoDecoder() {
}

FFVideoDecoder::~FFVideoDecoder() {
    release();
}

void FFVideoDecoder::extractCsd(AVCodecParameters *params, AMediaFormat *format) {
    if (!params->extradata || params->extradata_size <= 0) {
        LOGD("No extradata, skipping CSD setup (bitstream filter will carry SPS/PPS in data stream)");
        return;
    }

    // After h264_mp4toannexb / hevc_mp4toannexb bitstream filter,
    // extradata is already in AnnexB format (00 00 00 01 + SPS + 00 00 00 01 + PPS).
    // Set the entire extradata as csd-0, MediaCodec will parse SPS/PPS from it automatically.
    AMediaFormat_setBuffer(format, "csd-0", params->extradata, params->extradata_size);
    LOGD("Set csd-0 from extradata, size: %d", params->extradata_size);
}

int FFVideoDecoder::init(AVCodecParameters *codecParams, ANativeWindow *window) {
    if (!codecParams || !window) {
        LOGE("Invalid params: codecParams=%p, window=%p", codecParams, window);
        return -1;
    }

    nativeWindow = window;
    eos.store(false);

    // Determine MIME type
    const char *mime = "video/avc"; // H.264
    if (codecParams->codec_id == AV_CODEC_ID_HEVC) {
        mime = "video/hevc"; // H.265
    } else if (codecParams->codec_id == AV_CODEC_ID_VP9) {
        mime = "video/x-vnd.on2.vp9";
    } else if (codecParams->codec_id == AV_CODEC_ID_VP8) {
        mime = "video/x-vnd.on2.vp8";
    }

    LOGD("Initializing video decoder: mime=%s, %dx%d", mime, codecParams->width, codecParams->height);

    // Create MediaFormat
    AMediaFormat *format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, codecParams->width);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, codecParams->height);

    // Set SPS/PPS (csd-0 / csd-1)
    extractCsd(codecParams, format);

    // Set color info if available (AMEDIAFORMAT_KEY_COLOR_RANGE requires API 28+)
    if (codecParams->color_range != AVCOL_RANGE_UNSPECIFIED) {
#if __ANDROID_API__ >= 28
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_RANGE, codecParams->color_range);
#else
        AMediaFormat_setInt32(format, "color-range", codecParams->color_range);
#endif
    }

    // Create MediaCodec
    mediaCodec = AMediaCodec_createDecoderByType(mime);
    if (!mediaCodec) {
        LOGE("Failed to create MediaCodec decoder: %s", mime);
        AMediaFormat_delete(format);
        return -1;
    }

    // Configure MediaCodec (output to ANativeWindow)
    media_status_t status = AMediaCodec_configure(mediaCodec, format, window, nullptr, 0);
    AMediaFormat_delete(format);

    if (status != AMEDIA_OK) {
        LOGE("MediaCodec configure failed: %d", status);
        AMediaCodec_delete(mediaCodec);
        mediaCodec = nullptr;
        return -1;
    }

    // Note: When MediaCodec renders directly to ANativeWindow,
    // no need to manually set ANativeWindow_setBuffersGeometry, MediaCodec manages it automatically

    // Start decoder
    status = AMediaCodec_start(mediaCodec);
    if (status != AMEDIA_OK) {
        LOGE("MediaCodec start failed: %d", status);
        AMediaCodec_delete(mediaCodec);
        mediaCodec = nullptr;
        return -1;
    }

    LOGD("Video decoder initialized successfully");
    return 0;
}

int FFVideoDecoder::sendPacket(AVPacket *packet) {
    if (!mediaCodec) return -1;

    // EOS signal
    if (!packet || packet->size <= 0) {
        ssize_t bufIdx = AMediaCodec_dequeueInputBuffer(mediaCodec, DEQUEUE_TIMEOUT_US);
        if (bufIdx >= 0) {
            AMediaCodec_queueInputBuffer(mediaCodec, bufIdx, 0, 0, 0,
                                         AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
            LOGD("Sent EOS to video decoder");
        }
        return 0;
    }

    ssize_t bufIdx = AMediaCodec_dequeueInputBuffer(mediaCodec, DEQUEUE_TIMEOUT_US);
    if (bufIdx < 0) {
        // Input buffer temporarily unavailable, need to retry
        return 1;
    }

    size_t bufSize = 0;
    uint8_t *buf = AMediaCodec_getInputBuffer(mediaCodec, bufIdx, &bufSize);
    if (!buf) {
        LOGE("Failed to get input buffer");
        return -1;
    }

    // Copy packet data to MediaCodec input buffer
    size_t copySize = (packet->size < (int) bufSize) ? packet->size : bufSize;
    memcpy(buf, packet->data, copySize);

    // Calculate PTS (already converted to microseconds in readThreadFunc)
    int64_t ptsUs = 0;
    if (packet->pts != AV_NOPTS_VALUE) {
        ptsUs = packet->pts;
    }

    AMediaCodec_queueInputBuffer(mediaCodec, bufIdx, 0, copySize, ptsUs, 0);
    return 0;
}

int FFVideoDecoder::receiveFrame(int64_t &ptsUs) {
    ssize_t bufIdx = -1;
    int ret = dequeueFrame(ptsUs, bufIdx);
    if (ret == 0 && bufIdx >= 0) {
        releaseFrame(bufIdx, true);
    }
    return ret;
}

int FFVideoDecoder::receiveFrame(int64_t &ptsUs, bool render) {
    ssize_t bufIdx = -1;
    int ret = dequeueFrame(ptsUs, bufIdx);
    if (ret == 0 && bufIdx >= 0) {
        releaseFrame(bufIdx, render);
    }
    return ret;
}

int FFVideoDecoder::dequeueFrame(int64_t &ptsUs, ssize_t &outBufIdx) {
    if (!mediaCodec) return -1;

    outBufIdx = -1;
    AMediaCodecBufferInfo info;
    ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(mediaCodec, &info, DEQUEUE_TIMEOUT_US);

    if (outIdx >= 0) {
        ptsUs = info.presentationTimeUs;

        // Check EOS flag
        if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
            AMediaCodec_releaseOutputBuffer(mediaCodec, outIdx, false);
            eos.store(true);
            LOGD("Video decoder reached EOS");
            return -1;
        }

        outBufIdx = outIdx;
        return 0;

    } else if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        AMediaFormat *newFormat = AMediaCodec_getOutputFormat(mediaCodec);
        if (newFormat) {
            int32_t w = 0, h = 0;
            AMediaFormat_getInt32(newFormat, AMEDIAFORMAT_KEY_WIDTH, &w);
            AMediaFormat_getInt32(newFormat, AMEDIAFORMAT_KEY_HEIGHT, &h);
            LOGD("Output format changed: %dx%d", w, h);
            AMediaFormat_delete(newFormat);
        }
        return 1; // Need to try again

    } else if (outIdx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
        return 1; // Need to try again
    }

    // AMEDIACODEC_INFO_TRY_AGAIN_LATER or other
    return 1;
}

void FFVideoDecoder::releaseFrame(ssize_t bufIdx, bool render) {
    if (mediaCodec && bufIdx >= 0) {
        AMediaCodec_releaseOutputBuffer(mediaCodec, bufIdx, render);
    }
}

void FFVideoDecoder::flush() {
    if (mediaCodec) {
        AMediaCodec_flush(mediaCodec);
        eos.store(false);
        LOGD("Video decoder flushed");
    }
}

void FFVideoDecoder::release() {
    if (mediaCodec) {
        AMediaCodec_stop(mediaCodec);
        AMediaCodec_delete(mediaCodec);
        mediaCodec = nullptr;
        LOGD("Video decoder released");
    }
    nativeWindow = nullptr;
    eos.store(false);
}
