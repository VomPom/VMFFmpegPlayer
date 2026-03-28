#ifndef FFMPEG_PLAYER_FF_PLAYER_CALLBACK_H
#define FFMPEG_PLAYER_FF_PLAYER_CALLBACK_H

#include <string>
#include <cstdint>

/**
 * FFPlayerCallback - 播放器回调抽象接口
 *
 * 将回调机制从 FFPlayerContext 中解耦，使核心播放逻辑不直接依赖 JNI。
 * 具体实现（如 JNI 回调）通过子类完成。
 */
class FFPlayerCallback {
public:
    virtual ~FFPlayerCallback() = default;

    /** 准备完成回调 */
    virtual void onPrepared(int64_t durationMs) = 0;

    /** 播放完成回调 */
    virtual void onCompletion() = 0;

    /** 错误回调 */
    virtual void onError(int code, const std::string &msg) = 0;

    /** 播放进度回调 */
    virtual void onProgress(int64_t currentMs, int64_t totalMs) = 0;

    /** 视频尺寸变化回调 */
    virtual void onVideoSizeChanged(int width, int height) = 0;

    /** 播放状态变化回调 */
    virtual void onStateChanged(int state) = 0;
};

#endif // FFMPEG_PLAYER_FF_PLAYER_CALLBACK_H
