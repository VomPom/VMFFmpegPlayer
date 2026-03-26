#ifndef LIB_EFFECT_EFFECT_PIPELINE_H
#define LIB_EFFECT_EFFECT_PIPELINE_H

#include "i_effect.h"
#include <vector>
#include <memory>
#include <mutex>
#include <string>

/**
 * EffectPipeline - 特效管线
 *
 * 管理所有特效的注册、查询和处理调度。
 * 播放器核心模块通过管线与特效交互，而不直接依赖具体特效实现。
 *
 * 处理流程：
 *   1. 视频线程调用 processVideoTime() → 遍历所有 TIME 类特效
 *   2. 音频线程调用 processAudioData() → 遍历所有 AUDIO 类特效
 *   3. [未来] 渲染线程调用 processVideoPixel() → 遍历所有 VIDEO_PIXEL 类特效
 *
 * 线程安全：
 *   所有公开方法均通过 mutex 保护，可在多线程环境下安全调用。
 */
class EffectPipeline {
public:
    EffectPipeline();
    ~EffectPipeline();

    /**
     * 添加特效到管线
     * @param effect 特效实例（管线接管所有权）
     */
    void addEffect(std::shared_ptr<IEffect> effect);

    /**
     * 按名称移除特效
     * @param name 特效名称
     * @return 是否成功移除
     */
    bool removeEffect(const std::string &name);

    /**
     * 按名称查找特效
     * @return 特效指针，未找到返回 nullptr
     */
    std::shared_ptr<IEffect> findEffect(const std::string &name) const;

    /**
     * 获取所有特效列表
     */
    std::vector<std::shared_ptr<IEffect>> getEffects() const;

    /**
     * 清空所有特效
     */
    void clearEffects();

    // ==================== 处理调度 ====================

    /**
     * 处理视频帧时间轴（由视频线程调用）
     * 遍历所有 TIME 类型特效，依次处理 PTS
     *
     * @param originalPtsUs 原始视频帧 PTS（微秒）
     * @param result 输出最终处理结果
     */
    void processVideoTime(int64_t originalPtsUs, TimeEffectResult &result);

    /**
     * 处理音频数据（由音频线程调用）
     * 遍历所有 AUDIO 类型特效，依次处理音频缓冲区
     *
     * @param buffer 音频缓冲区
     * @return 处理后的数据大小
     */
    int processAudioData(AudioEffectBuffer &buffer);

    // ==================== 便捷查询 ====================

    /**
     * 获取当前综合播放速度因子
     * 遍历所有 TIME 类特效，取最终的速度因子（多个速度特效会叠加）
     *
     * @return 速度因子，1.0 = 正常速度
     */
    float getPlaybackSpeed() const;

    /**
     * 是否有任何特效处于启用状态
     */
    bool hasActiveEffects() const;

    /**
     * 重置所有特效状态（seek 或重新播放时调用）
     */
    void resetAll();

private:
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<IEffect>> effects_;
};

#endif // LIB_EFFECT_EFFECT_PIPELINE_H
