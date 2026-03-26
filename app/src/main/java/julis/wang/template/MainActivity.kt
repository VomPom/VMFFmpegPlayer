package julis.wang.template

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.vompom.ffmpegplayer.FFPlayer
import com.vompom.ffmpegplayer.FFPlayerView
import android.widget.AdapterView
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.TimeUnit

class MainActivity : AppCompatActivity(), FFPlayer.Listener {

    companion object {
        private const val TAG = "MainActivity"
        private const val REQUEST_PERMISSION_CODE = 100
    }

    // 视频源列表：显示名称 -> 播放地址
    private data class VideoSource(val name: String, val url: String)

    private val videoSources = mutableListOf<VideoSource>()

    private lateinit var playerView: FFPlayerView
    private lateinit var seekBar: SeekBar
    private lateinit var currentTimeText: TextView
    private lateinit var totalTimeText: TextView
    private lateinit var playPauseButton: Button
    private lateinit var scaleModeButton: Button
    private lateinit var videoSpinner: Spinner
    private lateinit var statusText: TextView

    private var player: FFPlayer? = null
    private var isSeeking = false
    private var currentVideoIndex = 0
    private var isUserSelectingVideo = false
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
        videoSpinner = findViewById(R.id.videoSpinner)
        statusText = findViewById(R.id.statusText)

        initVideoSources()
        setupVideoSpinner()
        setupListeners()
    }

    /**
     * 初始化视频源列表
     */
    private fun initVideoSources() {
        videoSources += listOf(
            // 本地视频（assets 目录中的不同格式文件）
            VideoSource("本地视频 (sample.mp4)", "sample.mp4"),
            VideoSource("本地视频 (hdr10-720p.mp4)", "hdr10-720p.mp4"),
            // 远程视频
            VideoSource(
                "远程视频 1",
                "https://user-images.githubusercontent.com/28951144/229373720-14d69157-1a56-4a78-a2f4-d7a134d7c3e9.mp4"
            ),
            VideoSource(
                "远程视频 2",
                "https://user-images.githubusercontent.com/28951144/229373695-22f88f13-d18f-4288-9bf1-c3e078d83722.mp4"
            ),
            VideoSource(
                "远程视频 3",
                "https://user-images.githubusercontent.com/28951144/229373709-603a7a89-2105-4e1b-a5a5-a6c3567c9a59.mp4"
            ),
            VideoSource(
                "远程视频 4",
                "https://user-images.githubusercontent.com/28951144/229373716-76da0a4e-225a-44e4-9ee7-3e9006dbc3e3.mp4"
            ),
            VideoSource(
                "远程视频 5",
                "https://user-images.githubusercontent.com/28951144/229373718-86ce5e1d-d195-45d5-baa6-ef94041d0b90.mp4"
            ),
        )
    }

    /**
     * 初始化视频选择下拉列表
     */
    private fun setupVideoSpinner() {
        val names = videoSources.map { it.name }
        val adapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, names)
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        videoSpinner.adapter = adapter

        videoSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: android.view.View?, position: Int, id: Long) {
                if (!isUserSelectingVideo) {
                    isUserSelectingVideo = true
                    return
                }
                if (position != currentVideoIndex) {
                    currentVideoIndex = position
                    switchVideo(position)
                }
            }

            override fun onNothingSelected(parent: AdapterView<*>?) {}
        }
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
            val source = videoSources[currentVideoIndex]
            val path: String

            if (source.url.startsWith("http://") || source.url.startsWith("https://")) {
                // 远程视频：直接使用 URL
                path = source.url
            } else {
                // 本地视频：从 assets 复制到缓存目录
                val cacheFile = File(cacheDir, source.url)
                if (!cacheFile.exists() || cacheFile.length() == 0L) {
                    assets.open(source.url).use { input ->
                        FileOutputStream(cacheFile).use { output ->
                            input.copyTo(output)
                        }
                    }
                }
                path = cacheFile.absolutePath
            }

            player?.setDataSource(path)
            player?.prepareAsync()

            statusText.text = "正在准备: ${source.name}"
        } catch (e: Exception) {
            Log.e(TAG, "加载视频失败", e)
            statusText.text = "加载视频失败: ${e.message}"
        }
    }

    /**
     * 切换视频源
     */
    private fun switchVideo(index: Int) {
        Log.d(TAG, "切换视频: ${videoSources[index].name}")
        statusText.text = "切换视频中..."

        // 停止当前播放
        updateHandler.removeCallbacks(updateRunnable)
        player?.stop()

        // 重置 UI
        seekBar.progress = 0
        currentTimeText.text = "00:00"
        totalTimeText.text = "00:00"
        playPauseButton.text = "播放"

        // 加载新视频
        loadVideo()
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