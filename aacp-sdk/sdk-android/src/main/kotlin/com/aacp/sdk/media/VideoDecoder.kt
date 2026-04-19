package com.aacp.sdk.media

import android.media.MediaCodec
import android.media.MediaFormat
import android.util.Log
import android.view.Surface
import java.util.concurrent.atomic.AtomicBoolean

/**
 * VideoDecoder — Decode H.264 Annex-B NALU stream từ CarPlay và render lên Surface.
 *
 * CarPlay gửi H.264 Annex-B với start code [00 00 00 01].
 * Thứ tự NALU:
 *   SPS (type=7) → PPS (type=8) → IDR (type=5) → P-frames (type=1) → ...
 *
 * Decoder được khởi tạo lazy — chờ SPS+PPS trước mới configure MediaCodec,
 * vì cần biết resolution từ SPS header.
 */
class VideoDecoder(private val surface: Surface) {

    companion object {
        private const val TAG = "AACP_Video"

        // NALU types (5-bit, byte[4] & 0x1F sau start code 4 bytes)
        private const val NALU_SPS = 7
        private const val NALU_PPS = 8
        private const val NALU_IDR = 5   // Keyframe
        private const val NALU_NON_IDR = 1

        // Start code Annex-B: 00 00 00 01
        private val START_CODE = byteArrayOf(0, 0, 0, 1)

        // Timeout dequeueing buffer (microseconds)
        private const val DEQUEUE_TIMEOUT_US = 10_000L   // 10ms
    }

    private var codec: MediaCodec? = null
    private var sps: ByteArray? = null
    private var pps: ByteArray? = null
    private val codecReady = AtomicBoolean(false)
    private val bufferInfo = MediaCodec.BufferInfo()

    // Stats
    var framesDecoded = 0L
        private set
    var framesDropped = 0L
        private set

    fun initialize() {
        // Tạo decoder object nhưng chưa configure
        // Configure sẽ được gọi sau khi nhận SPS+PPS
        Log.i(TAG, "VideoDecoder initialized (lazy configure)")
    }

    /**
     * Feed một NALU vào decoder.
     * Gọi từ native callback thread — NOT main thread.
     *
     * @param data         Raw bytes bao gồm 4-byte Annex-B start code + NALU data
     * @param timestampUs  Presentation timestamp (microseconds, monotonic)
     */
    fun feedNalu(data: ByteArray, timestampUs: Long) {
        if (data.size < 5) return  // Start code (4) + ít nhất 1 byte header

        // Byte thứ 4 (index 4) sau start code là NALU header byte
        val naluType = data[4].toInt() and 0x1F

        when (naluType) {
            NALU_SPS -> {
                // Lưu SPS (bỏ start code)
                sps = data.copyOfRange(4, data.size)
                Log.d(TAG, "SPS received: ${sps!!.size} bytes")
                tryConfigureCodec()
            }
            NALU_PPS -> {
                // Lưu PPS (bỏ start code)
                pps = data.copyOfRange(4, data.size)
                Log.d(TAG, "PPS received: ${pps!!.size} bytes")
                tryConfigureCodec()
            }
            NALU_IDR -> {
                if (codecReady.get()) {
                    submitFrame(data, timestampUs, isKeyFrame = true)
                } else {
                    Log.w(TAG, "IDR dropped — codec not ready yet")
                    framesDropped++
                }
            }
            NALU_NON_IDR -> {
                if (codecReady.get()) {
                    submitFrame(data, timestampUs, isKeyFrame = false)
                } else {
                    framesDropped++
                }
            }
            else -> Log.d(TAG, "Ignoring NALU type=$naluType")
        }
    }

    private fun tryConfigureCodec() {
        val currentSps = sps ?: return
        val currentPps = pps ?: return
        if (codecReady.get()) return  // Đã configure rồi

        try {
            val format = MediaFormat.createVideoFormat(
                MediaFormat.MIMETYPE_VIDEO_AVC,
                1280, 720   // Default CarPlay resolution; update nếu parse SPS
            ).apply {
                // csd-0 = SPS, csd-1 = PPS (không có start code)
                setByteBuffer("csd-0", java.nio.ByteBuffer.wrap(currentSps))
                setByteBuffer("csd-1", java.nio.ByteBuffer.wrap(currentPps))
                setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 2 * 1024 * 1024)  // 2MB
                // Low latency: giảm buffering, quan trọng cho interactive CarPlay
                setInteger(MediaFormat.KEY_LOW_LATENCY, 1)
                // Realtime priority
                if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.R) {
                    setInteger(MediaFormat.KEY_PRIORITY, 0)
                }
            }

            val newCodec = MediaCodec.createDecoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
            // Configure với surface → output frame trực tiếp lên screen (zero-copy)
            newCodec.configure(format, surface, null, 0)
            newCodec.start()

            codec = newCodec
            codecReady.set(true)
            Log.i(TAG, "H.264 decoder configured and started (1280x720, low-latency)")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to configure codec: ${e.message}")
        }
    }

    private fun submitFrame(data: ByteArray, timestampUs: Long, isKeyFrame: Boolean) {
        val c = codec ?: return

        // ── Enqueue input ─────────────────────────────────────────────────────
        val inputIdx = c.dequeueInputBuffer(DEQUEUE_TIMEOUT_US)
        if (inputIdx < 0) {
            // Decoder busy — drop frame (thường xảy ra khi overloaded)
            framesDropped++
            Log.v(TAG, "Input buffer unavailable — frame dropped")
            return
        }

        val inputBuf = c.getInputBuffer(inputIdx) ?: return
        inputBuf.clear()
        if (data.size > inputBuf.capacity()) {
            Log.w(TAG, "Frame too large: ${data.size} > ${inputBuf.capacity()} — dropping")
            c.queueInputBuffer(inputIdx, 0, 0, timestampUs, 0)
            framesDropped++
            return
        }
        inputBuf.put(data)

        val flags = if (isKeyFrame) MediaCodec.BUFFER_FLAG_KEY_FRAME else 0
        c.queueInputBuffer(inputIdx, 0, data.size, timestampUs, flags)

        // ── Dequeue output (non-blocking) ─────────────────────────────────────
        // Timeout = 0 → trả về ngay nếu không có output
        val outIdx = c.dequeueOutputBuffer(bufferInfo, 0)
        when {
            outIdx >= 0 -> {
                // render = true → frame được render lên surface tự động
                c.releaseOutputBuffer(outIdx, true)
                framesDecoded++
            }
            outIdx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                val newFormat = c.outputFormat
                Log.i(TAG, "Output format changed: $newFormat")
            }
            // INFO_TRY_AGAIN_LATER và INFO_OUTPUT_BUFFERS_CHANGED: bỏ qua
        }
    }

    fun release() {
        codecReady.set(false)
        try {
            codec?.stop()
            codec?.release()
        } catch (e: Exception) {
            Log.w(TAG, "Exception during release: ${e.message}")
        }
        codec = null
        sps = null
        pps = null
        Log.i(TAG, "VideoDecoder released. Decoded=$framesDecoded, Dropped=$framesDropped")
    }
}
