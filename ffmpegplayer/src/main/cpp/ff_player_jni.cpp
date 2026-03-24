#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <string>

#include "ff_player_context.h"

#define LOG_TAG "FFPlayerJNI"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// JNI 注册的目标 Java 类
static const char *CLASS_NAME = "com/vompom/ffmpegplayer/FFPlayer";
static JavaVM *g_javaVM = nullptr;

// ==================== 辅助函数 ====================

static FFPlayerContext *getNativePlayer(JNIEnv *env, jobject thiz) {
    jclass clazz = env->GetObjectClass(thiz);
    jfieldID fid = env->GetFieldID(clazz, "nativeHandle", "J");
    env->DeleteLocalRef(clazz);
    if (!fid) return nullptr;
    return reinterpret_cast<FFPlayerContext *>(env->GetLongField(thiz, fid));
}

static void setNativePlayer(JNIEnv *env, jobject thiz, FFPlayerContext *player) {
    jclass clazz = env->GetObjectClass(thiz);
    jfieldID fid = env->GetFieldID(clazz, "nativeHandle", "J");
    env->DeleteLocalRef(clazz);
    if (fid) {
        env->SetLongField(thiz, fid, reinterpret_cast<jlong>(player));
    }
}

// ==================== Native 方法实现 ====================

static void nativeInit(JNIEnv *env, jobject thiz) {
    auto *player = new FFPlayerContext(g_javaVM, thiz);
    setNativePlayer(env, thiz, player);
    LOGD("nativeInit: player=%p", player);
}

static jint nativePrepare(JNIEnv *env, jobject thiz, jstring path) {
    auto *player = getNativePlayer(env, thiz);
    if (!player) return -1;
    const char *pathStr = env->GetStringUTFChars(path, nullptr);
    std::string pathCpp(pathStr);
    env->ReleaseStringUTFChars(path, pathStr);
    return player->prepare(pathCpp);
}

static jint nativePrepareWithFd(JNIEnv *env, jobject thiz, jint fd) {
    auto *player = getNativePlayer(env, thiz);
    if (!player) return -1;
    return player->prepareWithFd(fd);
}

static void nativeStart(JNIEnv *env, jobject thiz) {
    auto *player = getNativePlayer(env, thiz);
    if (player) player->start();
}

static void nativePause(JNIEnv *env, jobject thiz) {
    auto *player = getNativePlayer(env, thiz);
    if (player) player->pause();
}

static void nativeResume(JNIEnv *env, jobject thiz) {
    auto *player = getNativePlayer(env, thiz);
    if (player) player->resume();
}

static void nativeStop(JNIEnv *env, jobject thiz) {
    auto *player = getNativePlayer(env, thiz);
    if (player) player->stop();
}

static void nativeReset(JNIEnv *env, jobject thiz) {
    auto *player = getNativePlayer(env, thiz);
    if (player) player->reset();
}

static void nativeSeekTo(JNIEnv *env, jobject thiz, jlong positionMs) {
    auto *player = getNativePlayer(env, thiz);
    if (player) player->seekTo(positionMs);
}

static void nativeRelease(JNIEnv *env, jobject thiz) {
    auto *player = getNativePlayer(env, thiz);
    if (player) {
        player->release();
        delete player;
        setNativePlayer(env, thiz, nullptr);
        LOGD("nativeRelease done");
    }
}

static void nativeSetSurface(JNIEnv *env, jobject thiz, jobject surface) {
    auto *player = getNativePlayer(env, thiz);
    if (player) {
        player->setSurface(env, surface);
    }
}

static jlong nativeGetDuration(JNIEnv *env, jobject thiz) {
    auto *player = getNativePlayer(env, thiz);
    return player ? player->getDuration() : 0;
}

static jlong nativeGetCurrentPosition(JNIEnv *env, jobject thiz) {
    auto *player = getNativePlayer(env, thiz);
    return player ? player->getCurrentPosition() : 0;
}

static jint nativeGetState(JNIEnv *env, jobject thiz) {
    auto *player = getNativePlayer(env, thiz);
    return player ? player->getState() : 0;
}

static jint nativeGetVideoWidth(JNIEnv *env, jobject thiz) {
    auto *player = getNativePlayer(env, thiz);
    return player ? player->getVideoWidth() : 0;
}

static jint nativeGetVideoHeight(JNIEnv *env, jobject thiz) {
    auto *player = getNativePlayer(env, thiz);
    return player ? player->getVideoHeight() : 0;
}

// ==================== 动态注册 ====================

static const JNINativeMethod g_methods[] = {
        {"nativeInit",               "()V",                       (void *) nativeInit},
        {"nativePrepare",            "(Ljava/lang/String;)I",     (void *) nativePrepare},
        {"nativePrepareWithFd",      "(I)I",                      (void *) nativePrepareWithFd},
        {"nativeStart",              "()V",                       (void *) nativeStart},
        {"nativePause",              "()V",                       (void *) nativePause},
        {"nativeResume",             "()V",                       (void *) nativeResume},
        {"nativeStop",               "()V",                       (void *) nativeStop},
        {"nativeSeekTo",             "(J)V",                      (void *) nativeSeekTo},
        {"nativeRelease",            "()V",                       (void *) nativeRelease},
        {"nativeReset",              "()V",                       (void *) nativeReset},
        {"nativeSetSurface",         "(Landroid/view/Surface;)V", (void *) nativeSetSurface},
        {"nativeGetDuration",        "()J",                       (void *) nativeGetDuration},
        {"nativeGetCurrentPosition", "()J",                       (void *) nativeGetCurrentPosition},
        {"nativeGetState",           "()I",                       (void *) nativeGetState},
        {"nativeGetVideoWidth",      "()I",                       (void *) nativeGetVideoWidth},
        {"nativeGetVideoHeight",     "()I",                       (void *) nativeGetVideoHeight},
};

/**
 * JNI_OnLoad: 缓存 JavaVM 指针并动态注册 native 方法
 */
JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    g_javaVM = vm;
    JNIEnv *env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) {
        LOGE("JNI_OnLoad: GetEnv failed");
        return JNI_ERR;
    }

    jclass clazz = env->FindClass(CLASS_NAME);
    if (!clazz) {
        LOGE("JNI_OnLoad: FindClass '%s' failed", CLASS_NAME);
        return JNI_ERR;
    }

    int methodCount = sizeof(g_methods) / sizeof(g_methods[0]);
    if (env->RegisterNatives(clazz, g_methods, methodCount) < 0) {
        LOGE("JNI_OnLoad: RegisterNatives failed");
        env->DeleteLocalRef(clazz);
        return JNI_ERR;
    }

    env->DeleteLocalRef(clazz);
    LOGD("JNI_OnLoad: registered %d methods for %s", methodCount, CLASS_NAME);
    return JNI_VERSION_1_6;
}