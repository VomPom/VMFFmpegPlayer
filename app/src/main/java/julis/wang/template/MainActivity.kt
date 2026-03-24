package julis.wang.template

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.widget.Button
import android.widget.SeekBar
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.vompom.ffmpegplayer.FFPlayer
import com.vompom.ffmpegplayer.FFPlayerView
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.TimeUnit

class MainActivity : AppCompatActivity(), FFPlayer.Listener {

    companion object {
        private const val TAG = "MainActivity"
        private const val REQUEST_PERMISSION_CODE = 100
        // 使用assets中的本地视频文件，因为FFmpeg没有启用HTTP/HTTPS协议
        private const val SAMPLE_VIDEO_PATH = "sample.mp4"
    }

    private lateinit var playerView: FFPlayerView
    private lateinit var seekBar: SeekBar
    private lateinit var currentTimeText: TextView
    private lateinit var totalTimeText: TextView
    private lateinit var playPauseButton: Button
    private lateinit var scaleModeButton: Button
    private lateinit var statusText: TextView

    private var player: FFPlayer? = null
    private var isSeeking = false
    private val updateHandler = Handler(Looper.getMainLooper())
    private val updateRunnable = object : Runnable {
        override fun run() {
            updateProgress()
            updateHandler.postDelayed(this, 1000) // 每秒更新一次
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        initViews()
        setupPlayer()
        checkPermissions()
    }

    private fun initViews() {

        playerView = findViewById<FFPlayerView>(R.id.playerView)
        seekBar = findViewById(R.id.seekBar)
        currentTimeText = findViewById(R.id.currentTimeText)
        totalTimeText = findViewById(R.id.totalTimeText)
        playPauseButton = findViewById(R.id.playPauseButton)
        scaleModeButton = findViewById(R.id.scaleModeButton)
        statusText = findViewById(R.id.statusText)

        setupListeners()
    }

    private fun setupListeners() {
        playPauseButton.setOnClickListener {
            togglePlayPause()
        }

        scaleModeButton.setOnClickListener {
            toggleScaleMode()
        }

        seekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                if (fromUser) {
                    currentTimeText.text = formatTime(progress.toLong())
                    // 暂停时拖动实时 seek，让画面跟着变
                    player?.let {
                        val state = it.getState()
                        if (state == FFPlayer.STATE_PAUSED || state == FFPlayer.STATE_PREPARED) {
                            it.seekTo(progress.toLong())
                        }
                    }
                }
            }

            override fun onStartTrackingTouch(seekBar: SeekBar?) {
                isSeeking = true
            }

            override fun onStopTrackingTouch(seekBar: SeekBar?) {
                isSeeking = false
                player?.seekTo(seekBar?.progress?.toLong() ?: 0)
            }
        })
    }

    private var pendingLoadVideo = false

    private fun setupPlayer() {
        if (!FFPlayer.nativeLoaded) {
            statusText.text = "FFmpeg库未加载，请检查编译配置"
            return
        }

        player = FFPlayer().apply {
            setListener(this@MainActivity)
        }

        playerView.bindPlayer(player!!)
        
        // Surface 就绪后再加载视频，确保 prepare 时 Surface 已经设置
        playerView.onSurfaceReady = {
            Log.d(TAG, "Surface 就绪")
            if (pendingLoadVideo) {
                pendingLoadVideo = false
                loadVideo()
            }
        }
        
        // 设置视频尺寸变化回调
        playerView.onVideoSizeChanged = { width, height ->
            runOnUiThread {
                statusText.text = "视频尺寸: ${width}x${height}"
                Log.d(TAG, "视频尺寸变化: ${width}x${height}")
            }
        }
        
        statusText.text = "播放器初始化完成"
    }

    private fun checkPermissions() {
        val permissions = arrayOf(
            Manifest.permission.READ_EXTERNAL_STORAGE,
            Manifest.permission.WRITE_EXTERNAL_STORAGE,
            Manifest.permission.INTERNET
        )

        val permissionsToRequest = permissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }

        if (permissionsToRequest.isNotEmpty()) {
            ActivityCompat.requestPermissions(
                this,
                permissionsToRequest.toTypedArray(),
                REQUEST_PERMISSION_CODE
            )
        } else {
            loadVideo()
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQUEST_PERMISSION_CODE) {
            if (grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
                loadVideo()
            } else {
                statusText.text = "权限被拒绝，无法播放视频"
            }
        }
    }

    private fun loadVideo() {
        // 如果 Surface 还没就绪，先标记为待加载，等 Surface 回调后再执行
        if (!playerView.isSurfaceReady) {
            Log.d(TAG, "Surface 未就绪，等待 Surface 回调后再加载视频")
            pendingLoadVideo = true
            statusText.text = "等待 Surface 就绪..."
            return
        }
        
        try {
            // 从assets加载本地视频文件
            val assetManager = assets
            val inputStream = assetManager.open(SAMPLE_VIDEO_PATH)
            val cacheFile = File(cacheDir, SAMPLE_VIDEO_PATH)
            
            // 将assets中的视频复制到缓存目录
            FileOutputStream(cacheFile).use { output ->
                inputStream.copyTo(output)
            }
            
            // 使用本地文件路径
            player?.setDataSource(cacheFile.absolutePath)
            player?.prepareAsync()
            statusText.text = "正在准备视频..."
        } catch (e: Exception) {
            Log.e(TAG, "加载视频失败", e)
            statusText.text = "加载视频失败: ${e.message}"
        }
    }

    private fun togglePlayPause() {
        player?.let {
            when (it.getState()) {
                FFPlayer.STATE_PLAYING -> {
                    it.pause()
                    playPauseButton.text = "播放"
                }
                FFPlayer.STATE_PAUSED, FFPlayer.STATE_PREPARED -> {
                    it.start()
                    playPauseButton.text = "暂停"
                }
                FFPlayer.STATE_COMPLETED -> {
                    // 播放完成，重置播放器状态后重新播放
                    Log.d(TAG, "播放完成，开始重置并重新播放")
                    it.reset()
                    it.start()
                    playPauseButton.text = "暂停"
                }
                else -> {
                    it.prepareAsync()
                    playPauseButton.text = "准备中..."
                }
            }
        }
    }

    private fun toggleScaleMode() {
        val currentMode = playerView.getCurrentScaleType()
        val nextMode = when (currentMode) {
            FFPlayerView.ScaleType.CENTER_INSIDE -> FFPlayerView.ScaleType.CENTER_CROP
            FFPlayerView.ScaleType.CENTER_CROP -> FFPlayerView.ScaleType.FIT_XY
            FFPlayerView.ScaleType.FIT_XY -> FFPlayerView.ScaleType.ORIGINAL
            FFPlayerView.ScaleType.ORIGINAL -> FFPlayerView.ScaleType.CENTER_INSIDE
        }
        
        playerView.setScaleType(nextMode)
        scaleModeButton.text = when (nextMode) {
            FFPlayerView.ScaleType.CENTER_INSIDE -> "居中显示"
            FFPlayerView.ScaleType.CENTER_CROP -> "裁剪填充"
            FFPlayerView.ScaleType.FIT_XY -> "拉伸铺满"
            FFPlayerView.ScaleType.ORIGINAL -> "原始尺寸"
        }
        statusText.text = "画面模式: ${scaleModeButton.text}"
    }

    private fun updateProgress() {
        if (isSeeking) return

        player?.let {
            val currentPos = it.getCurrentPosition()
            val duration = it.getDuration()

            if (duration > 0) {
                seekBar.max = duration.toInt()
                seekBar.progress = currentPos.toInt()
                currentTimeText.text = formatTime(currentPos)
                totalTimeText.text = formatTime(duration)
            }
        }
    }

    private fun formatTime(milliseconds: Long): String {
        val minutes = TimeUnit.MILLISECONDS.toMinutes(milliseconds)
        val seconds = TimeUnit.MILLISECONDS.toSeconds(milliseconds) - TimeUnit.MINUTES.toSeconds(minutes)
        return String.format("%02d:%02d", minutes, seconds)
    }

    // ==================== FFPlayer 回调 ====================

    override fun onPrepared(durationMs: Long) {
        Log.d(TAG, "onPrepared: duration=${durationMs}ms")
        runOnUiThread {
            seekBar.max = durationMs.toInt()
            totalTimeText.text = formatTime(durationMs)
            playPauseButton.text = "播放"
            statusText.text = "准备就绪，点击播放"
        }
    }

    override fun onProgress(currentMs: Long, totalMs: Long) {
        // 进度更新在updateRunnable中处理
    }

    override fun onCompletion() {
        Log.d(TAG, "onCompletion，自动重播")
        runOnUiThread {
            // 自动重播：重置播放器并重新开始播放
            player?.let {
                it.reset()
                it.start()
                playPauseButton.text = "暂停"
                seekBar.progress = 0
                currentTimeText.text = "00:00"
                statusText.text = "自动重播中"
                updateHandler.post(updateRunnable)
            }
        }
    }

    override fun onError(code: Int, message: String) {
        Log.e(TAG, "onError: code=$code, message=$message")
        runOnUiThread {
            statusText.text = "播放错误: $message"
            updateHandler.removeCallbacks(updateRunnable)
        }
    }

    override fun onVideoSizeChanged(width: Int, height: Int) {
        Log.d(TAG, "onVideoSizeChanged: ${width}x${height}")
        runOnUiThread {
            statusText.text = "视频尺寸: ${width}x${height}"
            playerView.setVideoSize(width, height)
        }
    }

    override fun onStateChanged(state: Int) {
        val stateText = when (state) {
            FFPlayer.STATE_IDLE -> "空闲"
            FFPlayer.STATE_PREPARING -> "准备中"
            FFPlayer.STATE_PREPARED -> "准备就绪"
            FFPlayer.STATE_PLAYING -> "播放中"
            FFPlayer.STATE_PAUSED -> "暂停"
            FFPlayer.STATE_STOPPED -> "停止"
            FFPlayer.STATE_COMPLETED -> "完成"
            FFPlayer.STATE_ERROR -> "错误"
            else -> "未知"
        }
        Log.d(TAG, "onStateChanged: $stateText")
        runOnUiThread {
            statusText.text = "状态: $stateText"
            
            when (state) {
                FFPlayer.STATE_PLAYING -> {
                    playPauseButton.text = "暂停"
                    updateHandler.post(updateRunnable)
                }
                FFPlayer.STATE_PAUSED, FFPlayer.STATE_STOPPED -> {
                    playPauseButton.text = "播放"
                    updateHandler.removeCallbacks(updateRunnable)
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        updateHandler.removeCallbacks(updateRunnable)
        player?.release()
        player = null
    }
}