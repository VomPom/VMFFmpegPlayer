#include "ff_jni_callback.h"
#include <cstdarg>

#define LOG_TAG "FFJniCallback"
#include "ff_log.h"

FFJniCallback::FFJniCallback(JavaVM *javaVM, jobject javaPlayer)
        : javaVM_(javaVM) {
    JNIEnv *env = getJNIEnv();
    if (env && javaPlayer) {
        javaPlayer_ = env->NewGlobalRef(javaPlayer);
    }
}

FFJniCallback::~FFJniCallback() {
    release();
}

JNIEnv *FFJniCallback::getJNIEnv() {
    JNIEnv *env = nullptr;
    if (javaVM_) {
        int status = javaVM_->GetEnv((void **) &env, JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            javaVM_->AttachCurrentThread(&env, nullptr);
        }
    }
    return env;
}

void FFJniCallback::callJavaMethod(const char *methodName, const char *sig, ...) {
    JNIEnv *env = getJNIEnv();
    if (!env || !javaPlayer_) return;

    jclass clazz = env->GetObjectClass(javaPlayer_);
    jmethodID method = env->GetMethodID(clazz, methodName, sig);
    if (method) {
        va_list args;
        va_start(args, sig);
        env->CallVoidMethodV(javaPlayer_, method, args);
        va_end(args);
    }
    env->DeleteLocalRef(clazz);
}

void FFJniCallback::onPrepared(int64_t durationMs) {
    callJavaMethod("onNativePrepared", "(J)V", durationMs);
}

void FFJniCallback::onCompletion() {
    callJavaMethod("onNativeCompletion", "()V");
}

void FFJniCallback::onError(int code, const std::string &msg) {
    JNIEnv *env = getJNIEnv();
    if (!env || !javaPlayer_) return;
    jstring jMsg = env->NewStringUTF(msg.c_str());
    callJavaMethod("onNativeError", "(ILjava/lang/String;)V", code, jMsg);
    env->DeleteLocalRef(jMsg);
}

void FFJniCallback::onProgress(int64_t currentMs, int64_t totalMs) {
    callJavaMethod("onNativeProgress", "(JJ)V", currentMs, totalMs);
}

void FFJniCallback::onVideoSizeChanged(int width, int height) {
    callJavaMethod("onNativeVideoSizeChanged", "(II)V", width, height);
}

void FFJniCallback::onStateChanged(int state) {
    callJavaMethod("onNativeStateChanged", "(I)V", state);
}

void FFJniCallback::release() {
    JNIEnv *env = getJNIEnv();
    if (env && javaPlayer_) {
        env->DeleteGlobalRef(javaPlayer_);
        javaPlayer_ = nullptr;
    }
}
