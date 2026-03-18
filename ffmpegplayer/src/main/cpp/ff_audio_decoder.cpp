#include "ff_audio_decoder.h"
#include <android/log.h>
#include <cstring>

#define LOG_TAG "FFAudioDecoder"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

FFAudioDecoder::FFAudioDecoder() {
}

FFAudioDecoder::~FFAudioDecoder() {
    release();
}

int FFAudioDecoder::init(AVCodecParameters *codecParams) {
    if (!codecParams) return -1;

    eos.store(false);

    // Find decoder
    const AVCodec *codec = avcodec_find_decoder(codecParams->codec_id);
    if (!codec) {
        LOGE("Audio decoder not found: codec_id=%d", codecParams->codec_id);
        return -1;
    }

    // Create decoder context
    codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        LOGE("Failed to allocate audio decoder context");
        return -1;
    }

    avcodec_parameters_to_context(codecCtx, codecParams);

    int ret = avcodec_open2(codecCtx, codec, nullptr);
    if (ret < 0) {
        LOGE("Failed to open audio decoder");
        avcodec_free_context(&codecCtx);
        return ret;
    }

    // Init resampler: convert decoded audio to unified PCM S16LE 44100Hz stereo
    outSampleRate = 44100;
    outChannels = 2;

    AVChannelLayout outChLayout;
    av_channel_layout_default(&outChLayout, outChannels);

    ret = swr_alloc_set_opts2(&swrCtx,
                              &outChLayout, AV_SAMPLE_FMT_S16, outSampleRate,
                              &codecCtx->ch_layout, codecCtx->sample_fmt, codecCtx->sample_rate,
                              0, nullptr);
    if (ret < 0 || !swrCtx) {
        LOGE("Failed to create resampler");
        avcodec_free_context(&codecCtx);
        return -1;
    }
    swr_init(swrCtx);

    av_channel_layout_uninit(&outChLayout);

    LOGD("Audio decoder initialized: sampleRate=%d, channels=%d, format=%d",
         codecCtx->sample_rate, codecCtx->ch_layout.nb_channels, codecCtx->sample_fmt);

    // Init OpenSL ES
    initOpenSLES();

    return 0;
}

void FFAudioDecoder::initOpenSLES() {
    SLresult result;

    // Create engine
    result = slCreateEngine(&engineObj, 0, nullptr, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("OpenSL ES engine creation failed");
        return;
    }
    (*engineObj)->Realize(engineObj, SL_BOOLEAN_FALSE);
    (*engineObj)->GetInterface(engineObj, SL_IID_ENGINE, &engineItf);

    // Create output mixer
    result = (*engineItf)->CreateOutputMix(engineItf, &outputMixObj, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Output mixer creation failed");
        return;
    }
    (*outputMixObj)->Realize(outputMixObj, SL_BOOLEAN_FALSE);

    // Create audio player
    SLDataLocator_AndroidSimpleBufferQueue locBufQ = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 4
    };
    SLDataFormat_PCM formatPcm = {
            SL_DATAFORMAT_PCM,
            (SLuint32) outChannels,
            SL_SAMPLINGRATE_44_1,
            SL_PCMSAMPLEFORMAT_FIXED_16,
            SL_PCMSAMPLEFORMAT_FIXED_16,
            (outChannels == 2) ? (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT)
                               : SL_SPEAKER_FRONT_CENTER,
            SL_BYTEORDER_LITTLEENDIAN
    };
    SLDataSource audioSrc = {&locBufQ, &formatPcm};

    SLDataLocator_OutputMix locOutMix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObj};
    SLDataSink audioSnk = {&locOutMix, nullptr};

    const SLInterfaceID ids[] = {SL_IID_BUFFERQUEUE};
    const SLboolean req[] = {SL_BOOLEAN_TRUE};

    result = (*engineItf)->CreateAudioPlayer(engineItf, &playerObj, &audioSrc, &audioSnk,
                                             1, ids, req);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Audio player creation failed: %d", result);
        return;
    }

    (*playerObj)->Realize(playerObj, SL_BOOLEAN_FALSE);
    (*playerObj)->GetInterface(playerObj, SL_IID_PLAY, &playItf);
    (*playerObj)->GetInterface(playerObj, SL_IID_BUFFERQUEUE, &bufferQueueItf);

    // Register buffer queue callback
    (*bufferQueueItf)->RegisterCallback(bufferQueueItf, bufferQueueCallback, this);

    // Start playback
    (*playItf)->SetPlayState(playItf, SL_PLAYSTATE_PLAYING);

    // Proactively enqueue a silence buffer to start the callback chain
    // OpenSL ES is callback-driven, must enqueue first to trigger subsequent callbacks
    std::vector<uint8_t> silenceBuffer(4096, 0);
    (*bufferQueueItf)->Enqueue(bufferQueueItf, silenceBuffer.data(), silenceBuffer.size());

    LOGD("OpenSL ES initialized successfully");
}

void FFAudioDecoder::bufferQueueCallback(SLAndroidSimpleBufferQueueItf bq, void *context) {
    auto *decoder = static_cast<FFAudioDecoder *>(context);
    decoder->onBufferQueueCallback();
}

void FFAudioDecoder::onBufferQueueCallback() {
    std::unique_lock<std::mutex> lock(queueMutex);
    if (bufferQueue.empty()) {
        // Queue empty, enqueue silence data to avoid stuttering
        playingBuffer.resize(4096, 0);
        (*bufferQueueItf)->Enqueue(bufferQueueItf, playingBuffer.data(), playingBuffer.size());
        return;
    }

    auto &front = bufferQueue.front();
    playingBuffer = std::move(front.data);
    audioClockUs.store(front.ptsUs);
    bufferQueue.pop();
    lock.unlock();

    queueCond.notify_one();

    (*bufferQueueItf)->Enqueue(bufferQueueItf, playingBuffer.data(), playingBuffer.size());
}

int FFAudioDecoder::sendPacket(AVPacket *packet) {
    if (!codecCtx) return -1;

    if (!packet) {
        // Send empty packet to signal EOS
        avcodec_send_packet(codecCtx, nullptr);
        return 0;
    }

    return avcodec_send_packet(codecCtx, packet);
}

int FFAudioDecoder::decodeFrame() {
    if (!codecCtx) return -1;

    AVFrame *frame = av_frame_alloc();
    int ret = avcodec_receive_frame(codecCtx, frame);

    if (ret == 0) {
        // Calculate output sample count
        int outSamples = swr_get_out_samples(swrCtx, frame->nb_samples);

        // Allocate output buffer
        int bufSize = av_samples_get_buffer_size(nullptr, outChannels, outSamples,
                                                 AV_SAMPLE_FMT_S16, 1);
        if (bufSize <= 0) bufSize = outSamples * outChannels * 2;

        AudioBuffer audioBuf;
        audioBuf.data.resize(bufSize);

        uint8_t *outBuf = audioBuf.data.data();
        int convertedSamples = swr_convert(swrCtx, &outBuf, outSamples,
                                           (const uint8_t **) frame->data, frame->nb_samples);

        if (convertedSamples > 0) {
            // Actual data size
            int actualSize = convertedSamples * outChannels * 2;
            audioBuf.data.resize(actualSize);

            // Calculate PTS (already converted to microseconds in readThreadFunc)
            if (frame->pts != AV_NOPTS_VALUE) {
                audioBuf.ptsUs = frame->pts;
            }

            // Push to playback queue
            std::unique_lock<std::mutex> lock(queueMutex);
            // Limit queue size to avoid memory explosion
            while (bufferQueue.size() >= 16) {
                queueCond.wait_for(lock, std::chrono::milliseconds(10));
            }
            bufferQueue.push(std::move(audioBuf));
        }

        av_frame_free(&frame);
        return 0;

    } else if (ret == AVERROR(EAGAIN)) {
        av_frame_free(&frame);
        return 1; // Need more data

    } else if (ret == AVERROR_EOF) {
        eos.store(true);
        av_frame_free(&frame);
        LOGD("Audio decoder reached EOS");
        return -1;
    }

    av_frame_free(&frame);
    return ret;
}

void FFAudioDecoder::pause() {
    if (playItf) {
        (*playItf)->SetPlayState(playItf, SL_PLAYSTATE_PAUSED);
    }
}

void FFAudioDecoder::resume() {
    if (playItf) {
        (*playItf)->SetPlayState(playItf, SL_PLAYSTATE_PLAYING);
    }
}

void FFAudioDecoder::flush() {
    if (codecCtx) {
        avcodec_flush_buffers(codecCtx);
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        while (!bufferQueue.empty()) {
            bufferQueue.pop();
        }
    }

    // Reset audio clock
    audioClockUs.store(0);
    eos.store(false);

    // Restart OpenSL ES callback chain: re-enqueue a silence buffer
    if (bufferQueueItf) {
        // Clear OpenSL ES buffer queue first
        (*bufferQueueItf)->Clear(bufferQueueItf);
        // Ensure playing state
        if (playItf) {
            (*playItf)->SetPlayState(playItf, SL_PLAYSTATE_PLAYING);
        }
        // Restart callback chain
        std::vector<uint8_t> silenceBuffer(4096, 0);
        (*bufferQueueItf)->Enqueue(bufferQueueItf, silenceBuffer.data(), silenceBuffer.size());
    }

    LOGD("Audio decoder flushed");
}

void FFAudioDecoder::release() {
    // Stop OpenSL ES
    if (playItf) {
        (*playItf)->SetPlayState(playItf, SL_PLAYSTATE_STOPPED);
    }
    if (playerObj) {
        (*playerObj)->Destroy(playerObj);
        playerObj = nullptr;
        playItf = nullptr;
        bufferQueueItf = nullptr;
    }
    if (outputMixObj) {
        (*outputMixObj)->Destroy(outputMixObj);
        outputMixObj = nullptr;
    }
    if (engineObj) {
        (*engineObj)->Destroy(engineObj);
        engineObj = nullptr;
        engineItf = nullptr;
    }

    // Release resampler
    if (swrCtx) {
        swr_free(&swrCtx);
        swrCtx = nullptr;
    }

    // Release decoder
    if (codecCtx) {
        avcodec_free_context(&codecCtx);
        codecCtx = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        while (!bufferQueue.empty()) {
            bufferQueue.pop();
        }
    }

    eos.store(false);
    LOGD("Audio decoder released");
}
