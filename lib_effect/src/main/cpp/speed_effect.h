#ifndef LIB_EFFECT_SPEED_EFFECT_H
#define LIB_EFFECT_SPEED_EFFECT_H

#include "i_effect.h"
#include <algorithm>
#include <mutex>

/**
 * SpeedEffect - 变速播放特效
 *
 * 将「快慢放」抽象为时间轴类特效，通过修改播放速度因子来影响：
 *   1. 视频帧渲染时机（通过 processTime 调整 PTS 间隔）
 *   2. 音频播放速率（通过 getSpeedFactor 供音频模块查询）
 *
 * 参数：
 *   - "speed": float, 播放速度 (0.25 ~ 4.0)，默认 1.0
 *
 * 设计说明：
 *   变速播放同时影响时间轴和音频，但本特效归类为 TIME 类型，
 *   因为它的核心逻辑是修改时间轴。音频变速由 EffectPipeline
 *   统一查询 speedFactor 后传递给音频处理模块。
 */
class SpeedEffect : public IEffect {
public:
    // 速度范围限制
    static constexpr float MIN_SPEED = 0.25f;
    static constexpr float MAX_SPEED = 4.0f;
    static constexpr float DEFAULT_SPEED = 1.0f;

    SpeedEffect();
    ~SpeedEffect() override = default;

    std::string getName() const override { return "speed"; }
    EffectType getType() const override { return EffectType::TIME; }

    void setParams(const EffectParams &params) override;
    EffectParams getParams() const override;

    /**
     * 时间轴处理：根据速度因子调整 PTS
     *
     * 原理：
     *   快放 (speed > 1.0)：PTS 间隔缩小 → 帧渲染更快 → 视觉上加速
     *   慢放 (speed < 1.0)：PTS 间隔放大 → 帧渲染更慢 → 视觉上减速
     *
     * 具体计算：
     *   adjustedPts = basePts + (originalPts - basePts) / speed
     *   其中 basePts 是 reset 后第一帧的 PTS
     */
    void processTime(int64_t originalPtsUs, TimeEffectResult &result) override;

    void reset() override;

    /** 获取当前速度因子（线程安全） */
    float getSpeedFactor() const;

    /** 便捷方法：直接设置速度 */
    void setSpeed(float speed);

private:
    mutable std::mutex mutex_;
    float speed_ = DEFAULT_SPEED;

    // 用于 PTS 重映射的基准点
    bool hasBasePts_ = false;
    int64_t basePtsOriginal_ = 0;   // 原始时间轴的基准 PTS
    int64_t basePtsAdjusted_ = 0;   // 调整后时间轴的基准 PTS
};

#endif // LIB_EFFECT_SPEED_EFFECT_H
