package com.aacp.sdk.media

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.media.MediaCodec
import android.media.MediaFormat
import android.util.Log
import java.nio.ByteBuffer

/**
 * AudioPlayer — Decode AAC-LC từ CarPlay và phát qua AudioTrack.
 *
 * CarPlay audio spec:
 *   - Codec:       AAC-LC (Low Complexity)
 *   - Sample rate: 44100 Hz
 *   - Channels:    Stereo (2)
 *   - Bit depth:   16-bit PCM (sau decode)
 *   - Packet:      ADTS-wrapped hoặc raw AAC frames
 *
 * Pipeline: raw AAC bytes → MediaCodec(AAC decoder) → PCM → AudioTrack → speaker
 */
class AudioPlayer {

    companion object {
        private const val TAG = "AACP_Audio"

        // CarPlay audio parameters (fixed)
        const val SAMPLE_RATE   = 44100
        const val CHANNEL_COUNT = 2
        const val BIT_DEPTH     = 16

        // AudioTrack buffer = 4× min để absorb jitter
        const val BUFFER_MULTIPLIER = 4

        private const val DEQUEUE_TIMEOUT_US = 5_000L  // 5ms
    }

    private var aacDecoder: MediaCodec? = null
    private var audioTrack: AudioTrack? = null
    private val decodeInfo = MediaCodec.BufferInfo()

    // Stats
    var packetsDecoded = 0L
        private set
    var underrunCount  = 0
        private set

    /**
     * Khởi tạo AAC decoder và AudioTrack.
     * Phải gọi trước feedAacData().
     */
    fun initialize() {
        initAacDecoder()
        initAudioTrack()
        Log.i(TAG, "AudioPlayer initialized (${SAMPLE_RATE}Hz, stereo, 16-bit)")
    }

    private fun initAacDecoder() {
        // AAC-LC MediaCodec decoder
        val decoder = MediaCodec.createDecoderByType(MediaFormat.MIMETYPE_AUDIO_AAC)

        val format = MediaFormat.createAudioFormat(
            MediaFormat.MIMETYPE_AUDIO_AAC,
            SAMPLE_RATE,
            CHANNEL_COUNT
        ).apply {
            setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 16384)  // 16KB per packet
            setInteger(MediaFormat.KEY_AAC_PROFILE,
                android.media.MediaCodecInfo.CodecProfileLevel.AACObjectLC)

            // AudioSpecificConfig (ASC) — 2 bytes cho AAC-LC 44100Hz stereo
            // Bit layout: [5-bit objectType=2][4-bit freqIdx=4][4-bit channelCfg=2][3-bit ext=0]
            // objectType=2 (LC), freqIdx=4 (44100Hz), channelCfg=2 (stereo)
            // Binary: 00010 0100 0010 000 → 0x1210 → bytes: 0x12, 0x10
            val asc = ByteArray(2)
            asc[0] = 0x12.toByte()
            asc[1] = 0x10.toByte()
            setByteBuffer("csd-0", ByteBuffer.wrap(asc))
        }

        decoder.configure(format, null, null, 0)
        decoder.start()
        aacDecoder = decoder
        Log.d(TAG, "AAC decoder started")
    }

    private fun initAudioTrack() {
        val minBufSize = AudioTrack.getMinBufferSize(
            SAMPLE_RATE,
            AudioFormat.CHANNEL_OUT_STEREO,
            AudioFormat.ENCODING_PCM_16BIT
        )

        val attrs = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_MEDIA)
            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
            .build()

        val audioFmt = AudioFormat.Builder()
            .setSampleRate(SAMPLE_RATE)
            .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
            .build()

        val track = AudioTrack.Builder()
            .setAudioAttributes(attrs)
            .setAudioFormat(audioFmt)
            .setBufferSizeInBytes(minBufSize * BUFFER_MULTIPLIER)
            .setTransferMode(AudioTrack.MODE_STREAM)
            // LOW_LATENCY: giảm latency thay vì POWER_SAVING
            .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
            .build()

        track.play()
        audioTrack = track
        underrunCount = track.underrunCount
        Log.d(TAG, "AudioTrack started (minBuf=${minBufSize}, actual=${minBufSize * BUFFER_MULTIPLIER})")
    }

    /**
     * Feed một AAC packet vào decoder.
     * Gọi từ native audio callback thread.
     *
     * @param data        Raw AAC bytes (ADTS hoặc raw AAC-LC)
     * @param timestampUs Presentation timestamp
     */
    fun feedAacData(data: ByteArray, timestampUs: Long) {
        val decoder = aacDecoder ?: return
        val track   = audioTrack ?: return

        // ── Enqueue input buffer ──────────────────────────────────────────────
        val inputIdx = decoder.dequeueInputBuffer(DEQUEUE_TIMEOUT_US)
        if (inputIdx < 0) {
            Log.v(TAG, "No input buffer available — dropping audio packet")
            return
        }

        val inputBuf = decoder.getInputBuffer(inputIdx) ?: return
        inputBuf.clear()
        val writeLen = minOf(data.size, inputBuf.capacity())
        inputBuf.put(data, 0, writeLen)
        decoder.queueInputBuffer(inputIdx, 0, writeLen, timestampUs, 0)

        // ── Dequeue output (PCM) ──────────────────────────────────────────────
        val outIdx = decoder.dequeueOutputBuffer(decodeInfo, DEQUEUE_TIMEOUT_US)
        when {
            outIdx >= 0 -> {
                val outBuf = decoder.getOutputBuffer(outIdx)
                if (outBuf != null && decodeInfo.size > 0) {
                    val pcm = ByteArray(decodeInfo.size)
                    outBuf.position(decodeInfo.offset)
                    outBuf.get(pcm, 0, decodeInfo.size)

                    // Write PCM to AudioTrack (non-blocking)
                    val written = track.write(pcm, 0, pcm.size,
                        AudioTrack.WRITE_NON_BLOCKING)
                    if (written < 0) {
                        Log.w(TAG, "AudioTrack.write error: $written")
                    }
                    packetsDecoded++
                }
                decoder.releaseOutputBuffer(outIdx, false)

                // Kiểm tra underrun
                val currentUnderrun = track.underrunCount
                if (currentUnderrun > underrunCount) {
                    Log.w(TAG, "Audio underrun detected (count=$currentUnderrun)")
                    underrunCount = currentUnderrun
                }
            }
            outIdx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                val fmt = decoder.outputFormat
                Log.i(TAG, "Audio output format: $fmt")
            }
        }
    }

    /** Tạm dừng phát audio */
    fun pause() {
        audioTrack?.pause()
    }

    /** Tiếp tục phát audio */
    fun resume() {
        audioTrack?.play()
    }

    fun release() {
        try {
            aacDecoder?.stop()
            aacDecoder?.release()
        } catch (e: Exception) {
            Log.w(TAG, "aacDecoder release: ${e.message}")
        }
        try {
            audioTrack?.stop()
            audioTrack?.release()
        } catch (e: Exception) {
            Log.w(TAG, "audioTrack release: ${e.message}")
        }
        aacDecoder = null
        audioTrack = null
        Log.i(TAG, "AudioPlayer released. Decoded=$packetsDecoded, Underruns=$underrunCount")
    }
}
