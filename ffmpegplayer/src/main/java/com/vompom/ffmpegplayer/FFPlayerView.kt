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
     * 调整画面宽高比，铺满显示
     */
    private fun adjustAspectRatio() {
        if (videoWidth <= 0 || videoHeight <= 0 || width <= 0 || height <= 0) return

        val viewWidth = width.toFloat()
        val viewHeight = height.toFloat()
        val videoRatio = videoWidth.toFloat() / videoHeight.toFloat()
        val viewRatio = viewWidth / viewHeight

        val matrix = Matrix()
        
        if (videoRatio > viewRatio) {
            // 视频更宽，按宽度缩放
            val scale = viewWidth / videoWidth
            matrix.setScale(scale, scale)
            // 垂直居中
            val scaledHeight = videoHeight * scale
            val translateY = (viewHeight - scaledHeight) / 2
            matrix.postTranslate(0f, translateY)
        } else {
            // 视频更高，按高度缩放
            val scale = viewHeight / videoHeight
            matrix.setScale(scale, scale)
            // 水平居中
            val scaledWidth = videoWidth * scale
            val translateX = (viewWidth - scaledWidth) / 2
            matrix.postTranslate(translateX, 0f)
        }

        setTransform(matrix)
        Log.d(TAG, "调整画面缩放: 视频=${videoWidth}x${videoHeight}, 视图=${viewWidth.toInt()}x${viewHeight.toInt()}")
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
