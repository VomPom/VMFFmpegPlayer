#ifndef LIB_EFFECT_I_EFFECT_H
#define LIB_EFFECT_I_EFFECT_H

#include <string>
#include <cstdint>
#include <unordered_map>
#include <variant>
#include <vector>

/**
 * 特效类型枚举
 * 按处理阶段分类，便于管线按顺序调度
 */
enum class EffectType {
    // 时间轴类特效（影响播放速度、帧率等）
    TIME = 0,
    // 音频处理类特效（影响音频数据，如变速不变调、混音等）
    AUDIO = 1,
    // 视频像素类特效（影响视频帧像素，如滤镜、色彩调整等）
    VIDEO_PIXEL = 2,
    // 转场类特效（影响帧序列，如淡入淡出、滑动等）
    TRANSITION = 3,
};

/**
 * 特效参数值类型
 * 支持 float / int / bool / string 四种基本类型
 */
using EffectParamValue = std::variant<float, int, bool, std::string>;

/**
 * 特效参数表
 */
using EffectParams = std::unordered_map<std::string, EffectParamValue>;

/**
 * 音频数据缓冲区（用于音频特效处理）
 */
struct AudioEffectBuffer {
    uint8_t *data = nullptr;    // PCM 数据指针
    int size = 0;               // 数据大小（字节）
    int sampleRate = 44100;     // 采样率
    int channels = 2;           // 声道数
    int bitsPerSample = 16;     // 位深
    int64_t ptsUs = 0;          // 时间戳（微秒）
};

/**
 * 时间轴处理结果
 */
struct TimeEffectResult {
    float speedFactor = 1.0f;       // 播放速度因子（1.0 = 正常速度）
    int64_t adjustedPtsUs = 0;      // 调整后的 PTS（微秒）
    bool shouldDrop = false;        // 是否应丢弃该帧
};

/**
 * IEffect - 特效基类接口
 *
 * 所有特效必须继承此接口，实现对应的处理方法。
 * 特效管线会根据 getType() 返回的类型，调用对应的处理方法：
 *   - TIME 类型 → processTime()
 *   - AUDIO 类型 → processAudio()
 *   - VIDEO_PIXEL / TRANSITION → 未来扩展
 */
class IEffect {
public:
    virtual ~IEffect() = default;

    /** 获取特效唯一标识名 */
    virtual std::string getName() const = 0;

    /** 获取特效类型 */
    virtual EffectType getType() const = 0;

    /** 是否启用 */
    virtual bool isEnabled() const { return enabled_; }

    /** 设置启用/禁用 */
    virtual void setEnabled(bool enabled) { enabled_ = enabled; }

    /**
     * 设置特效参数
     * @param params 参数键值对
     */
    virtual void setParams(const EffectParams &params) = 0;

    /**
     * 获取当前参数
     */
    virtual EffectParams getParams() const = 0;

    /**
     * 时间轴处理（TIME 类型特效实现）
     * @param originalPtsUs 原始 PTS（微秒）
     * @param result 输出处理结果
     */
    virtual void processTime(int64_t originalPtsUs, TimeEffectResult &result) {
        result.speedFactor = 1.0f;
        result.adjustedPtsUs = originalPtsUs;
        result.shouldDrop = false;
    }

    /**
     * 音频数据处理（AUDIO 类型特效实现）
     * @param buffer 音频缓冲区（可原地修改）
     * @return 处理后的数据大小，<= 0 表示丢弃
     */
    virtual int processAudio(AudioEffectBuffer &buffer) {
        return buffer.size;
    }

    /**
     * 重置特效状态（seek 或重新播放时调用）
     */
    virtual void reset() {}

protected:
    bool enabled_ = true;
};

#endif // LIB_EFFECT_I_EFFECT_H
