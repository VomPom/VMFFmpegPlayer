package com.vompom.ffmpegplayer

import android.content.Context
import android.graphics.Matrix
import android.util.AttributeSet
import android.util.Log
import android.view.Surface
import android.view.TextureView

/**
 * FFPlayerView - 用于显示视频画面的 TextureView
 *
 * 支持画面缩放和旋转，自动管理 Surface 生命周期，绑定到 FFPlayer
 */
class FFPlayerView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyle: Int = 0
) : TextureView(context, attrs, defStyle), TextureView.SurfaceTextureListener {

    companion object {
        private const val TAG = "FFPlayerView"
    }

    private var player: FFPlayer? = null
    var isSurfaceReady = false
        private set
    private var videoWidth = 0
    private var videoHeight = 0

    /**
     * Surface 就绪回调（用于外部监听）
     */
    var onSurfaceReady: (() -> Unit)? = null

    /**
     * Surface 销毁回调
     */
    var onSurfaceDestroyed: (() -> Unit)? = null

    /**
     * 视频尺寸变化回调
     */
    var onVideoSizeChanged: ((width: Int, height: Int) -> Unit)? = null

    init {
        surfaceTextureListener = this
    }

    /**
     * 绑定播放器
     */
    fun bindPlayer(player: FFPlayer) {
        this.player = player
        if (isSurfaceReady) {
            player.setSurface(Surface(surfaceTexture))
        }
    }

    /**
     * 解绑播放器
     */
    fun unbindPlayer() {
        player?.setSurface(null)
        player = null
    }

    /**
     * 设置视频尺寸，自动调整画面缩放
     */
    fun setVideoSize(width: Int, height: Int) {
        if (width <= 0 || height <= 0) return
        
        videoWidth = width
        videoHeight = height
        adjustAspectRatio()
        onVideoSizeChanged?.invoke(width, height)
    }

    /**
     * 画面适配模式
     */
    enum class ScaleType {
        CENTER_INSIDE,    // 保持宽高比，完整显示视频内容（默认）
        CENTER_CROP,      // 保持宽高比，裁剪视频内容以填充视图
        FIT_XY,           // 拉伸铺满整个视图（会变形）
        ORIGINAL          // 原始尺寸显示
    }

    private var scaleType = ScaleType.CENTER_INSIDE

    /**
     * 设置画面适配模式
     */
    fun setScaleType(type: ScaleType) {
        scaleType = type
        adjustAspectRatio()
    }

    /**
     * 获取当前画面适配模式
     */
    fun getCurrentScaleType(): ScaleType = scaleType

    /**
     * 调整画面宽高比，根据适配模式显示
     *
     * 注意：TextureView 默认会将内容拉伸到整个 View 尺寸（等同于 FIT_XY），
     * 因此 Matrix 变换是基于已经拉伸到 viewWidth x viewHeight 的画面来做的，
     * 需要先"还原"到正确的宽高比，再进行缩放和位移。
     */
    private fun adjustAspectRatio() {
        if (videoWidth <= 0 || videoHeight <= 0 || width <= 0 || height <= 0) return

        val viewWidth = width.toFloat()
        val viewHeight = height.toFloat()
        val videoRatio = videoWidth.toFloat() / videoHeight.toFloat()
        val viewRatio = viewWidth / viewHeight

        val matrix = Matrix()

        when (scaleType) {
            ScaleType.FIT_XY -> {
                // 拉伸铺满整个视图（会变形）— TextureView 默认行为，单位矩阵即可
                matrix.reset()
            }
            ScaleType.CENTER_INSIDE -> {
                // 保持宽高比，完整显示视频内容，居中并留黑边
                val scaleX: Float
                val scaleY: Float
                if (videoRatio > viewRatio) {
                    // 视频更宽，以宽度为基准适配
                    scaleX = 1f
                    scaleY = viewRatio / videoRatio
                } else {
                    // 视频更高，以高度为基准适配
                    scaleX = videoRatio / viewRatio
                    scaleY = 1f
                }
                matrix.setScale(scaleX, scaleY, viewWidth / 2f, viewHeight / 2f)
            }
            ScaleType.CENTER_CROP -> {
                // 保持宽高比，裁剪视频内容以填充视图
                val scaleX: Float
                val scaleY: Float
                if (videoRatio > viewRatio) {
                    // 视频更宽，以高度填满，宽度溢出裁剪
                    scaleX = videoRatio / viewRatio
                    scaleY = 1f
                } else {
                    // 视频更高，以宽度填满，高度溢出裁剪
                    scaleX = 1f
                    scaleY = viewRatio / videoRatio
                }
                matrix.setScale(scaleX, scaleY, viewWidth / 2f, viewHeight / 2f)
            }
            ScaleType.ORIGINAL -> {
                // 原始尺寸显示，居中
                val scaleX = videoWidth.toFloat() / viewWidth
                val scaleY = videoHeight.toFloat() / viewHeight
                matrix.setScale(scaleX, scaleY, viewWidth / 2f, viewHeight / 2f)
            }
        }

        setTransform(matrix)
        Log.d(TAG, "调整画面缩放: 视频=${videoWidth}x${videoHeight}, 视图=${viewWidth.toInt()}x${viewHeight.toInt()}, 模式=$scaleType")
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        adjustAspectRatio()
    }

    override fun onSurfaceTextureAvailable(surface: android.graphics.SurfaceTexture, width: Int, height: Int) {
        Log.d(TAG, "onSurfaceTextureAvailable: ${width}x${height}")
        isSurfaceReady = true
        player?.setSurface(Surface(surface))
        onSurfaceReady?.invoke()
    }

    override fun onSurfaceTextureSizeChanged(surface: android.graphics.SurfaceTexture, width: Int, height: Int) {
        Log.d(TAG, "onSurfaceTextureSizeChanged: ${width}x${height}")
        adjustAspectRatio()
    }

    override fun onSurfaceTextureDestroyed(surface: android.graphics.SurfaceTexture): Boolean {
        Log.d(TAG, "onSurfaceTextureDestroyed")
        isSurfaceReady = false
        player?.setSurface(null)
        onSurfaceDestroyed?.invoke()
        return true
    }

    override fun onSurfaceTextureUpdated(surface: android.graphics.SurfaceTexture) {
        // 画面更新回调
    }
}
