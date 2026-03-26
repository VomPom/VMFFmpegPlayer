#include "effect_pipeline.h"
#include "speed_effect.h"
#include <android/log.h>
#include <algorithm>

#define LOG_TAG "EffectPipeline"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

EffectPipeline::EffectPipeline() {
}

EffectPipeline::~EffectPipeline() {
    clearEffects();
}

void EffectPipeline::addEffect(std::shared_ptr<IEffect> effect) {
    if (!effect) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // 检查是否已存在同名特效
    for (auto &existing : effects_) {
        if (existing->getName() == effect->getName()) {
            LOGD("替换已存在的特效: %s", effect->getName().c_str());
            existing = effect;
            return;
        }
    }

    effects_.push_back(effect);

    // 按特效类型排序，确保处理顺序：TIME → AUDIO → VIDEO_PIXEL → TRANSITION
    std::sort(effects_.begin(), effects_.end(),
              [](const std::shared_ptr<IEffect> &a, const std::shared_ptr<IEffect> &b) {
                  return static_cast<int>(a->getType()) < static_cast<int>(b->getType());
              });

    LOGD("添加特效: %s (类型=%d), 当前特效数: %zu",
         effect->getName().c_str(), (int) effect->getType(), effects_.size());
}

bool EffectPipeline::removeEffect(const std::string &name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::remove_if(effects_.begin(), effects_.end(),
                             [&name](const std::shared_ptr<IEffect> &e) {
                                 return e->getName() == name;
                             });

    if (it != effects_.end()) {
        effects_.erase(it, effects_.end());
        LOGD("移除特效: %s, 剩余特效数: %zu", name.c_str(), effects_.size());
        return true;
    }
    return false;
}

std::shared_ptr<IEffect> EffectPipeline::findEffect(const std::string &name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &effect : effects_) {
        if (effect->getName() == name) {
            return effect;
        }
    }
    return nullptr;
}

std::vector<std::shared_ptr<IEffect>> EffectPipeline::getEffects() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return effects_;
}

void EffectPipeline::clearEffects() {
    std::lock_guard<std::mutex> lock(mutex_);
    effects_.clear();
    LOGD("清空所有特效");
}

// ==================== 处理调度 ====================

void EffectPipeline::processVideoTime(int64_t originalPtsUs, TimeEffectResult &result) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 初始化结果
    result.speedFactor = 1.0f;
    result.adjustedPtsUs = originalPtsUs;
    result.shouldDrop = false;

    for (auto &effect : effects_) {
        if (effect->getType() == EffectType::TIME && effect->isEnabled()) {
            TimeEffectResult stepResult;
            effect->processTime(result.adjustedPtsUs, stepResult);

            // 叠加结果
            result.adjustedPtsUs = stepResult.adjustedPtsUs;
            result.speedFactor *= stepResult.speedFactor;
            result.shouldDrop = result.shouldDrop || stepResult.shouldDrop;
        }
    }
}

int EffectPipeline::processAudioData(AudioEffectBuffer &buffer) {
    std::lock_guard<std::mutex> lock(mutex_);

    int resultSize = buffer.size;
    for (auto &effect : effects_) {
        if (effect->getType() == EffectType::AUDIO && effect->isEnabled()) {
            resultSize = effect->processAudio(buffer);
            if (resultSize <= 0) break; // 数据被丢弃
        }
    }
    return resultSize;
}

// ==================== 便捷查询 ====================

float EffectPipeline::getPlaybackSpeed() const {
    std::lock_guard<std::mutex> lock(mutex_);

    float speed = 1.0f;
    for (auto &effect : effects_) {
        if (effect->getType() == EffectType::TIME && effect->isEnabled()) {
            // 尝试转换为 SpeedEffect 以直接获取速度因子
            auto *speedEffect = dynamic_cast<SpeedEffect *>(effect.get());
            if (speedEffect) {
                speed *= speedEffect->getSpeedFactor();
            }
        }
    }
    return speed;
}

bool EffectPipeline::hasActiveEffects() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &effect : effects_) {
        if (effect->isEnabled()) return true;
    }
    return false;
}

void EffectPipeline::resetAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &effect : effects_) {
        effect->reset();
    }
    LOGD("所有特效已重置");
}
