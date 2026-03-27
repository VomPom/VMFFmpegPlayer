package com.vompom.effect

/**
 * 特效类型枚举
 * 与 C++ 层 EffectType 一一对应
 */
enum class EffectType(val value: Int) {
    /** 时间轴类特效（影响播放速度、帧率等） */
    TIME(0),
    /** 音频处理类特效（影响音频数据，如变速不变调、混音等） */
    AUDIO(1),
    /** 视频像素类特效（影响视频帧像素，如滤镜、色彩调整等） */
    VIDEO_PIXEL(2),
    /** 转场类特效（影响帧序列，如淡入淡出、滑动等） */
    TRANSITION(3);

    companion object {
        fun fromValue(value: Int): EffectType? = values().firstOrNull { it.value == value }
    }
}
