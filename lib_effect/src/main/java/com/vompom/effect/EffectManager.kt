package com.vompom.effect

import com.vompom.ffmpegplayer.FFPlayer

/**
 * EffectManager - 特效管理器（Kotlin 层）
 *
 * 作为 lib_effect 模块中特效管理的统一入口，
 * 负责特效的注册、查找、速度控制等操作。
 *
 * 使用示例：
 * ```kotlin
 * val effectManager = EffectManager(player)
 * effectManager.setSpeed(2.0f)
 * ```
 */
class EffectManager(private val player: FFPlayer) {

    /** 内置变速特效 */
    val speedEffect = SpeedEffect()

    /** 所有已注册的特效 */
    private val effects = mutableMapOf<String, IEffect>()

    init {
        // 注册内置特效
        registerEffect(speedEffect)

        // 设置速度变化回调，自动同步到 Native 层
        speedEffect.onSpeedChanged = { speed ->
            player.setSpeed(speed)
        }
    }

    /**
     * 注册特效
     */
    fun registerEffect(effect: IEffect) {
        effects[effect.name] = effect
    }

    /**
     * 移除特效
     */
    fun removeEffect(name: String): Boolean {
        return effects.remove(name) != null
    }

    /**
     * 按名称查找特效
     */
    fun findEffect(name: String): IEffect? = effects[name]

    /**
     * 获取所有特效
     */
    fun getAllEffects(): List<IEffect> = effects.values.toList()

    // ==================== 便捷方法 ====================

    /**
     * 设置播放速度
     * @param speed 速度因子 (0.25 ~ 4.0)
     */
    fun setSpeed(speed: Float) {
        speedEffect.speed = speed
    }

    /**
     * 获取当前播放速度
     */
    fun getSpeed(): Float = speedEffect.speed

    /**
     * 重置所有特效
     */
    fun resetAll() {
        effects.values.forEach { it.reset() }
        player.setSpeed(1.0f)
    }
}