package com.vompom.effect

/**
 * SpeedEffect - 变速播放特效（Kotlin 封装层）
 *
 * 将「快慢放」抽象为时间轴类特效，通过 FFPlayer.setSpeed() 传递到 C++ 层。
 *
 * 支持速度范围：0.25x ~ 4.0x
 *
 * 使用示例：
 * ```kotlin
 * val speedEffect = SpeedEffect()
 *
 * // 方式1：直接设置速度
 * speedEffect.speed = 2.0f
 *
 * // 方式2：通过参数设置
 * speedEffect.setParams(mapOf("speed" to 2.0f))
 *
 * // 应用到播放器
 * player.setSpeed(speedEffect.speed)
 * ```
 *
 * 预设速度：
 * ```kotlin
 * SpeedEffect.SPEED_0_5X   // 0.5倍慢放
 * SpeedEffect.SPEED_1X     // 正常速度
 * SpeedEffect.SPEED_1_5X   // 1.5倍速
 * SpeedEffect.SPEED_2X     // 2倍速
 * ```
 */
class SpeedEffect : IEffect {

    companion object {
        const val EFFECT_NAME = "speed"

        /** 最小速度 */
        const val MIN_SPEED = 0.25f
        /** 最大速度 */
        const val MAX_SPEED = 4.0f

        // 预设速度常量
        const val SPEED_0_25X = 0.25f
        const val SPEED_0_5X = 0.5f
        const val SPEED_0_75X = 0.75f
        const val SPEED_1X = 1.0f
        const val SPEED_1_25X = 1.25f
        const val SPEED_1_5X = 1.5f
        const val SPEED_2X = 2.0f
        const val SPEED_3X = 3.0f
        const val SPEED_4X = 4.0f

        /**
         * 预设速度列表（用于 UI 展示）
         */
        val PRESET_SPEEDS = listOf(
            SPEED_0_5X to "0.5x",
            SPEED_0_75X to "0.75x",
            SPEED_1X to "1.0x",
            SPEED_1_25X to "1.25x",
            SPEED_1_5X to "1.5x",
            SPEED_2X to "2.0x",
            SPEED_3X to "3.0x",
        )

        /**
         * 获取速度的显示文本
         */
        fun getSpeedLabel(speed: Float): String {
            return PRESET_SPEEDS.find { it.first == speed }?.second
                ?: String.format("%.2fx", speed)
        }
    }

    override val name: String = EFFECT_NAME
    override val type: EffectType = EffectType.TIME
    override var enabled: Boolean = true

    /**
     * 当前播放速度
     * 设置时会自动 clamp 到 [MIN_SPEED, MAX_SPEED] 范围
     */
    var speed: Float = SPEED_1X
        set(value) {
            field = value.coerceIn(MIN_SPEED, MAX_SPEED)
            onSpeedChanged?.invoke(field)
        }

    /**
     * 速度变化回调
     * 当速度被修改时触发，可用于更新 UI 或同步到播放器
     */
    var onSpeedChanged: ((Float) -> Unit)? = null

    override fun setParams(params: Map<String, Any>) {
        (params["speed"] as? Number)?.let {
            speed = it.toFloat()
        }
    }

    override fun getParams(): Map<String, Any> {
        return mapOf("speed" to speed)
    }

    override fun reset() {
        speed = SPEED_1X
    }
}
