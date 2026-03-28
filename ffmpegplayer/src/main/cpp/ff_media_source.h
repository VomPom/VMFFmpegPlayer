#ifndef FFMPEG_PLAYER_FF_MEDIA_SOURCE_H
#define FFMPEG_PLAYER_FF_MEDIA_SOURCE_H

#include "ff_demuxer.h"
#include "ff_video_decoder.h"
#include "ff_audio_decoder.h"
#include "ff_timeline.h"

#include <atomic>
#include <mutex>
#include <android/native_window.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavutil/avutil.h>
}

/**
 * FFMediaSource - 单个片段的完整媒体源
 *
 * 封装一个视频片段的全部解码管线：
 *   Demuxer + BSF + VideoDecoder + AudioDecoder
 *
 * 职责：
 *   1. 打开/关闭源文件
 *   2. 管理 BSF（AVCC → AnnexB 转换）
 *   3. 管理视频/音频解码器的生命周期
 *   4. 提供 PTS 映射（源文件 PTS → 时间线 PTS）
 *   5. 提供源文件的流信息查询
 *
 * 多片段串联时，每个片段对应一个 FFMediaSource 实例。
 * 读取线程在片段切换时，关闭旧的 MediaSource 并打开新的。
 */
class FFMediaSource {
public:
    FFMediaSource();
    ~FFMediaSource();

    /**
     * 打开媒体源
     * @param clip 片段描述（包含源路径和时间映射信息）
     * @param nativeWindow 视频输出窗口（可为 nullptr，表示无视频输出）
     * @return 0 成功，负值失败
     */
    int open(const ClipDescriptor &clip, ANativeWindow *nativeWindow);

    /**
     * 关闭并释放所有资源
     */
    void close();

    /**
     * 获取片段描述
     */
    const ClipDescriptor &getClip() const { return clip_; }

    // ==================== Demuxer 访问 ====================

    FFDemuxer *getDemuxer() { return demuxer_; }

    int getVideoStreamIndex() const;
    int getAudioStreamIndex() const;
    AVRational getVideoTimeBase() const;
    AVRational getAudioTimeBase() const;

    // ==================== 解码器访问 ====================

    FFVideoDecoder *getVideoDecoder() { return videoDecoder_; }
    FFAudioDecoder *getAudioDecoder() { return audioDecoder_; }

    // ==================== BSF 访问 ====================

    AVBSFContext *getBSFContext() { return bsfCtx_; }

    // ==================== 信息查询 ====================

    int getWidth() const;
    int getHeight() const;
    double getVideoFps() const;

    /**
     * 将源文件 PTS（微秒）映射到项目时间线 PTS（微秒）
     */
    int64_t mapToTimelinePts(int64_t srcPtsUs) const {
        return clip_.srcToTimelinePts(srcPtsUs);
    }

    /**
     * 判断给定的源文件 PTS 是否超出了片段的裁剪范围
     * @param srcPtsUs 源文件 PTS（微秒）
     * @return true 表示已超出 srcEndUs，应停止读取此片段
     */
    bool isSourcePtsBeyondClip(int64_t srcPtsUs) const {
        return clip_.srcEndUs > 0 && srcPtsUs >= clip_.srcEndUs;
    }

    /**
     * Seek 到片段的起始位置（srcStartUs）
     * @return 0 成功
     */
    int seekToClipStart();

    /**
     * 中止 demuxer 的网络 IO
     */
    void abort();

    /**
     * 是否已打开
     */
    bool isOpened() const { return demuxer_ != nullptr; }

private:
    ClipDescriptor clip_;

    FFDemuxer *demuxer_ = nullptr;
    FFVideoDecoder *videoDecoder_ = nullptr;
    FFAudioDecoder *audioDecoder_ = nullptr;
    AVBSFContext *bsfCtx_ = nullptr;

    /**
     * 初始化 BSF
     */
    int initBSF();

    /**
     * 释放 BSF
     */
    void releaseBSF();
};

#endif // FFMPEG_PLAYER_FF_MEDIA_SOURCE_H
