package com.vompom.effect

/**
 * IEffect - 特效基类接口（Kotlin 层）
 *
 * 所有 Kotlin 层特效的统一抽象接口。
 * 具体的特效实现通过 C++ 层的 EffectPipeline 执行，
 * Kotlin 层主要负责参数管理和 API 暴露。
 *
 * 使用示例：
 * ```kotlin
 * val speedEffect = SpeedEffect()
 * speedEffect.setSpeed(2.0f)  // 2倍速
 * player.setSpeed(speedEffect.speed)
 * ```
 */
interface IEffect {

    /** 特效唯一标识名 */
    val name: String

    /** 特效类型 */
    val type: EffectType

    /** 是否启用 */
    var enabled: Boolean

    /**
     * 设置特效参数
     * @param params 参数键值对
     */
    fun setParams(params: Map<String, Any>)

    /**
     * 获取当前参数
     * @return 参数键值对
     */
    fun getParams(): Map<String, Any>

    /**
     * 重置特效状态
     */
    fun reset()
}
