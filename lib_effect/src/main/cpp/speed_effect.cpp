#include "speed_effect.h"
#include <android/log.h>

#define LOG_TAG "SpeedEffect"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

SpeedEffect::SpeedEffect() {
}

void SpeedEffect::setParams(const EffectParams &params) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = params.find("speed");
    if (it != params.end()) {
        if (auto *val = std::get_if<float>(&it->second)) {
            float oldSpeed = speed_;
            speed_ = std::clamp(*val, MIN_SPEED, MAX_SPEED);
            if (speed_ != oldSpeed) {
                // 速度变化时，需要重新建立基准点，避免 PTS 跳变
                // 下一次 processTime 会自动重建基准
                hasBasePts_ = false;
                LOGD("速度变更: %.2f → %.2f", oldSpeed, speed_);
            }
        }
    }
}

EffectParams SpeedEffect::getParams() const {
    std::lock_guard<std::mutex> lock(mutex_);
    EffectParams params;
    params["speed"] = speed_;
    return params;
}

void SpeedEffect::processTime(int64_t originalPtsUs, TimeEffectResult &result) {
    std::lock_guard<std::mutex> lock(mutex_);

    result.shouldDrop = false;
    result.speedFactor = speed_;

    if (!isEnabled() || speed_ == 1.0f) {
        // 未启用或正常速度，直接透传
        result.adjustedPtsUs = originalPtsUs;
        return;
    }

    // 建立基准点（首帧或速度变化后）
    if (!hasBasePts_) {
        basePtsOriginal_ = originalPtsUs;
        basePtsAdjusted_ = originalPtsUs;
        hasBasePts_ = true;
        result.adjustedPtsUs = originalPtsUs;
        return;
    }

    // PTS 重映射：
    // 原始间隔 = originalPts - basePtsOriginal
    // 调整间隔 = 原始间隔 / speed（快放缩短间隔，慢放拉长间隔）
    // 调整后PTS = basePtsAdjusted + 调整间隔
    int64_t originalDelta = originalPtsUs - basePtsOriginal_;
    int64_t adjustedDelta = (int64_t) ((double) originalDelta / (double) speed_);
    result.adjustedPtsUs = basePtsAdjusted_ + adjustedDelta;
}

void SpeedEffect::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    hasBasePts_ = false;
    basePtsOriginal_ = 0;
    basePtsAdjusted_ = 0;
    LOGD("SpeedEffect 已重置");
}

float SpeedEffect::getSpeedFactor() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return isEnabled() ? speed_ : 1.0f;
}

void SpeedEffect::setSpeed(float speed) {
    EffectParams params;
    params["speed"] = std::clamp(speed, MIN_SPEED, MAX_SPEED);
    setParams(params);
}
