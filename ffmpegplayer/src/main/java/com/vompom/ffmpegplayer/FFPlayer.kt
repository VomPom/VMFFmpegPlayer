package com.vompom.ffmpegplayer

import android.content.Context
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Surface

/**
 * FFPlayer - 基于 FFmpeg + MediaCodec H.264 硬解的视频播放器
 *
 * 架构:
 *   Kotlin(FFPlayer) --JNI--> C++(FFPlayerContext)
 *     --> FFDemuxer(FFmpeg 解封装)
 *     --> FFVideoDecoder(MediaCodec H.264 硬解 → ANativeWindow)
 *     --> FFAudioDecoder(FFmpeg 软解 → OpenSL ES)
 *     --> FFAVSync(音频时钟同步)
 *
 * 使用方式:
 *   val player = FFPlayer()
 *   player.setSurface(surface)
 *   player.setDataSource(context, uri)  // 或 setDataSource(path)
 *   player.prepare()
 *   player.start()
 */
class FFPlayer {

    companion object {
        private const val TAG = "FFPlayer"

        // 播放器状态常量（与 C++ FFPlayerState 对应）
        const val STATE_IDLE = 0
        const val STATE_PREPARING = 1
        const val STATE_PREPARED = 2
        const val STATE_PLAYING = 3
        const val STATE_PAUSED = 4
        const val STATE_STOPPED = 5
        const val STATE_COMPLETED = 6
        const val STATE_ERROR = 7

        /** native 库是否加载成功 */
        var nativeLoaded: Boolean = false
            private set

        init {
            try {
                System.loadLibrary("avutil")
                System.loadLibrary("swresample")
                System.loadLibrary("swscale")
                System.loadLibrary("avcodec")
                System.loadLibrary("avformat")
                System.loadLibrary("ffplayer")
                nativeLoaded = true
                Log.d(TAG, "Native 库加载成功")
            } catch (e: UnsatisfiedLinkError) {
                nativeLoaded = false
                Log.e(TAG, "Native 库加载失败（FFmpeg 预编译库未就绪）: ${e.message}")
            }
        }
    }

    /**
     * 播放器回调接口
     */
    interface Listener {
        fun onPrepared(durationMs: Long) {}
        fun onProgress(currentMs: Long, totalMs: Long) {}
        fun onCompletion() {}
        fun onError(code: Int, message: String) {}
        fun onVideoSizeChanged(width: Int, height: Int) {}
        fun onStateChanged(state: Int) {}
    }

    // Native 指针（C++ 层使用）
    @Suppress("unused")
    private var nativeHandle: Long = 0

    private var listener: Listener? = null
    private val mainHandler = Handler(Looper.getMainLooper())

    // 数据源
    private var dataSourcePath: String? = null
    private var dataSourceFd: Int = -1

    init {
        if (nativeLoaded) {
            nativeInit()
        } else {
            Log.w(TAG, "跳过 nativeInit: native 库未加载")
        }
    }

    /**
     * 设置回调监听
     */
    fun setListener(listener: Listener) {
        this.listener = listener
    }

    /**
     * 设置 Surface（来自 SurfaceView 或 TextureView）
     */
    fun setSurface(surface: Surface?) {
        if (!nativeLoaded) return
        nativeSetSurface(surface)
    }

    /**
     * 通过文件路径设置数据源
     */
    fun setDataSource(path: String) {
        this.dataSourcePath = path
        this.dataSourceFd = -1
    }

    /**
     * 通过 content:// URI 设置数据源
     * 内部通过 ContentResolver 获取文件描述符
     */
    fun setDataSource(context: Context, uri: Uri) {
        try {
            val pfd = context.contentResolver.openFileDescriptor(uri, "r")
            if (pfd != null) {
                this.dataSourceFd = pfd.detachFd()
                this.dataSourcePath = null
                Log.d(TAG, "通过 URI 获取到 fd: $dataSourceFd")
            } else {
                Log.e(TAG, "无法打开 URI: $uri")
            }
        } catch (e: Exception) {
            Log.e(TAG, "设置数据源失败", e)
            // 回退到路径方式
            this.dataSourcePath = uri.path
        }
    }

    /**
     * 准备播放（同步，会在当前线程阻塞）
     */
    fun prepare(): Int {
        if (!nativeLoaded) {
            Log.e(TAG, "prepare 失败: native 库未加载")
            return -1
        }
        return if (dataSourceFd >= 0) {
            nativePrepareWithFd(dataSourceFd)
        } else if (dataSourcePath != null) {
            nativePrepare(dataSourcePath!!)
        } else {
            Log.e(TAG, "未设置数据源")
            -1
        }
    }

    /**
     * 异步准备（在后台线程执行 prepare）
     */
    fun prepareAsync() {
        Thread {
            prepare()
        }.start()
    }

    /**
     * 开始播放
     */
    fun start() {
        if (!nativeLoaded) return
        nativeStart()
    }

    /**
     * 暂停
     */
    fun pause() {
        if (!nativeLoaded) return
        nativePause()
    }

    /**
     * 恢复播放（从暂停状态）
     */
    fun resume() {
        if (!nativeLoaded) return
        nativeResume()
    }

    /**
     * 停止播放
     */
    fun stop() {
        if (!nativeLoaded) return
        nativeStop()
    }

    /**
     * Seek 到指定位置
     * @param positionMs 目标位置（毫秒）
     */
    fun seekTo(positionMs: Long) {
        if (!nativeLoaded) return
        nativeSeekTo(positionMs)
    }

    /**
     * 重置播放器状态以便重新播放（不释放资源）
     */
    fun reset() {
        if (!nativeLoaded) return
        nativeReset()
    }

    /**
     * 释放所有资源
     */
    fun release() {
        if (!nativeLoaded) return
        nativeRelease()
    }

    /**
     * 获取视频总时长（毫秒）
     */
    fun getDuration(): Long = if (nativeLoaded) nativeGetDuration() else 0L

    /**
     * 获取当前播放位置（毫秒）
     */
    fun getCurrentPosition(): Long = if (nativeLoaded) nativeGetCurrentPosition() else 0L

    /**
     * 获取当前播放器状态
     */
    fun getState(): Int = if (nativeLoaded) nativeGetState() else STATE_IDLE

    /**
     * 获取视频宽度
     */
    fun getVideoWidth(): Int = if (nativeLoaded) nativeGetVideoWidth() else 0

    /**
     * 获取视频高度
     */
    fun getVideoHeight(): Int = if (nativeLoaded) nativeGetVideoHeight() else 0

    /**
     * 是否正在播放
     */
    fun isPlaying(): Boolean = nativeLoaded && getState() == STATE_PLAYING

    // ==================== C++ 回调（由 JNI 层调用） ====================

    @Suppress("unused")
    private fun onNativePrepared(durationMs: Long) {
        Log.d(TAG, "onNativePrepared: duration=${durationMs}ms")
        mainHandler.post {
            listener?.onPrepared(durationMs)
        }
    }

    @Suppress("unused")
    private fun onNativeCompletion() {
        Log.d(TAG, "onNativeCompletion")
        mainHandler.post {
            listener?.onCompletion()
        }
    }

    @Suppress("unused")
    private fun onNativeError(code: Int, message: String) {
        Log.e(TAG, "onNativeError: code=$code, msg=$message")
        mainHandler.post {
            listener?.onError(code, message)
        }
    }

    @Suppress("unused")
    private fun onNativeProgress(currentMs: Long, totalMs: Long) {
        mainHandler.post {
            listener?.onProgress(currentMs, totalMs)
        }
    }

    @Suppress("unused")
    private fun onNativeVideoSizeChanged(width: Int, height: Int) {
        Log.d(TAG, "onNativeVideoSizeChanged: ${width}x${height}")
        mainHandler.post {
            listener?.onVideoSizeChanged(width, height)
        }
    }

    @Suppress("unused")
    private fun onNativeStateChanged(state: Int) {
        mainHandler.post {
            listener?.onStateChanged(state)
        }
    }

    // ==================== Native 方法声明 ====================

    private external fun nativeInit()
    private external fun nativePrepare(path: String): Int
    private external fun nativePrepareWithFd(fd: Int): Int
    private external fun nativeStart()
    private external fun nativePause()
    private external fun nativeResume()
    private external fun nativeStop()
    private external fun nativeSeekTo(positionMs: Long)
    private external fun nativeRelease()
    private external fun nativeReset()
    private external fun nativeSetSurface(surface: Surface?)
    private external fun nativeGetDuration(): Long
    private external fun nativeGetCurrentPosition(): Long
    private external fun nativeGetState(): Int
    private external fun nativeGetVideoWidth(): Int
    private external fun nativeGetVideoHeight(): Int
}
