/**
 * 公共日志宏定义
 *
 * 使用方式：
 *   #define LOG_TAG "YourModuleName"
 *   #include "ff_log.h"
 *
 * 使用方在 include 本文件之前必须先定义 LOG_TAG，否则会编译报错。
 */

#ifndef FF_LOG_H
#define FF_LOG_H

#include <android/log.h>

#ifndef LOG_TAG
#error "请在 #include \"ff_log.h\" 之前定义 LOG_TAG"
#endif

#ifndef LOGD
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#endif

#ifndef LOGE
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#endif

#ifndef LOGI
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#endif

#ifndef LOGW
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#endif

#endif // FF_LOG_H
