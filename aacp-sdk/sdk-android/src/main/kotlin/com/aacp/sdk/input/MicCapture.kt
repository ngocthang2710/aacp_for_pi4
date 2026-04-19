package com.aacp.sdk.input

import android.Manifest
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.util.Log
import androidx.annotation.RequiresPermission
import java.util.concurrent.atomic.AtomicBoolean

/**
 * MicCapture — Thu âm PCM từ microphone và gửi lên iPhone (cho Siri).
 *
 * Spec:
 *   - Sample rate:  16000 Hz (16 kHz)
 *   - Channels:     Mono
 *   - Bit depth:    16-bit signed PCM, big-endian
 *   - Frame size:   1024 samples = 2048 bytes = ~64ms
 *
 * Yêu cầu permission: android.permission.RECORD_AUDIO
 * Check permission trước khi gọi start().
 */
class MicCapture(
    private val onPcmData: (ByteArray) -> Unit
) {

    companion object {
        private const val TAG = "AACP_Mic"

        const val SAMPLE_RATE   = 16000
        const val CHANNEL_CONFIG = AudioFormat.CHANNEL_IN_MONO
        const val AUDIO_FORMAT  = AudioFormat.ENCODING_PCM_16BIT

        // Số samples mỗi frame (1024 samples × 2 bytes = 2048 bytes = ~64ms)
        const val FRAME_SAMPLES = 1024
        const val FRAME_BYTES   = FRAME_SAMPLES * 2   // 16-bit = 2 bytes/sample
    }

    private var audioRecord: AudioRecord? = null
    private val recording = AtomicBoolean(false)
    private var captureThread: Thread? = null

    var bytesCapured = 0L
        private set

    /**
     * Bắt đầu thu âm.
     * @throws SecurityException nếu không có RECORD_AUDIO permission
     */
    @RequiresPermission(Manifest.permission.RECORD_AUDIO)
    fun start() {
        if (recording.get()) {
            Log.w(TAG, "Already recording")
            return
        }

        val minBufSize = AudioRecord.getMinBufferSize(
            SAMPLE_RATE, CHANNEL_CONFIG, AUDIO_FORMAT
        )
        if (minBufSize == AudioRecord.ERROR_BAD_VALUE ||
            minBufSize == AudioRecord.ERROR) {
            Log.e(TAG, "Invalid AudioRecord params")
            return
        }

        // Buffer size = max(minBuf, 4×FRAME_BYTES) để tránh overflow
        val bufSize = maxOf(minBufSize, FRAME_BYTES * 4)

        val record = AudioRecord(
            MediaRecorder.AudioSource.VOICE_COMMUNICATION,  // Echo cancellation
            SAMPLE_RATE,
            CHANNEL_CONFIG,
            AUDIO_FORMAT,
            bufSize
        )

        if (record.state != AudioRecord.STATE_INITIALIZED) {
            Log.e(TAG, "AudioRecord init failed (state=${record.state})")
            record.release()
            return
        }

        record.startRecording()
        audioRecord = record
        recording.set(true)

        // Capture thread: MAX_PRIORITY để tránh audio glitch
        captureThread = Thread({
            val buf = ByteArray(FRAME_BYTES)
            Log.i(TAG, "Capture thread started (${SAMPLE_RATE}Hz, mono, 16-bit)")

            while (recording.get()) {
                val read = record.read(buf, 0, buf.size)
                when {
                    read > 0 -> {
                        // CarPlay expect big-endian PCM — Android là little-endian
                        // Swap bytes: [lo, hi] → [hi, lo] cho mỗi sample
                        val swapped = swapBytes(buf, read)
                        onPcmData(swapped)
                        bytesCapured += read
                    }
                    read == AudioRecord.ERROR_INVALID_OPERATION -> {
                        Log.e(TAG, "AudioRecord.read: INVALID_OPERATION")
                        break
                    }
                    read == AudioRecord.ERROR_BAD_VALUE -> {
                        Log.e(TAG, "AudioRecord.read: BAD_VALUE")
                        break
                    }
                }
            }
            Log.i(TAG, "Capture thread exited. Total: ${bytesCapured / 1024}KB")
        }, "MicCaptureThread").also {
            it.priority = Thread.MAX_PRIORITY
            it.isDaemon = true
            it.start()
        }
    }

    fun stop() {
        if (!recording.getAndSet(false)) return
        captureThread?.join(500)  // Chờ tối đa 500ms
        captureThread = null

        audioRecord?.apply {
            stop()
            release()
        }
        audioRecord = null
        Log.i(TAG, "MicCapture stopped")
    }

    fun isRecording(): Boolean = recording.get()

    /**
     * Swap little-endian PCM sang big-endian.
     * Mỗi 16-bit sample: [byte_lo, byte_hi] → [byte_hi, byte_lo]
     */
    private fun swapBytes(src: ByteArray, length: Int): ByteArray {
        val dst = ByteArray(length)
        var i = 0
        while (i < length - 1) {
            dst[i]   = src[i + 1]
            dst[i+1] = src[i]
            i += 2
        }
        return dst
    }
}
