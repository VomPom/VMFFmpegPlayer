#ifndef FFMPEG_PLAYER_FF_JNI_CALLBACK_H
#define FFMPEG_PLAYER_FF_JNI_CALLBACK_H

#include "ff_player_callback.h"
#include <jni.h>

/**
 * FFJniCallback - JNI 回调实现
 *
 * 通过 JNI 将播放器事件回调到 Java 层。
 * 封装了 JavaVM 线程绑定、JNI 方法调用等细节。
 */
class FFJniCallback : public FFPlayerCallback {
public:
    FFJniCallback(JavaVM *javaVM, jobject javaPlayer);
    ~FFJniCallback() override;

    void onPrepared(int64_t durationMs) override;
    void onCompletion() override;
    void onError(int code, const std::string &msg) override;
    void onProgress(int64_t currentMs, int64_t totalMs) override;
    void onVideoSizeChanged(int width, int height) override;
    void onStateChanged(int state) override;

    /** 释放 Java 全局引用（release 时调用） */
    void release();

private:
    JavaVM *javaVM_ = nullptr;
    jobject javaPlayer_ = nullptr;  // GlobalRef

    /** 获取当前线程的 JNIEnv（自动 AttachCurrentThread） */
    JNIEnv *getJNIEnv();

    /** 通用 JNI 方法调用辅助函数 */
    void callJavaMethod(const char *methodName, const char *sig, ...);
};

#endif // FFMPEG_PLAYER_FF_JNI_CALLBACK_H
