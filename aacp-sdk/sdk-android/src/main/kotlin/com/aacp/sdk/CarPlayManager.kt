package com.aacp.sdk

import android.content.Context
import android.util.Log
import android.view.MotionEvent
import android.view.Surface
import com.aacp.sdk.input.MicCapture
import com.aacp.sdk.input.TouchForwarder
import com.aacp.sdk.media.AudioPlayer
import com.aacp.sdk.media.VideoDecoder
import java.util.concurrent.atomic.AtomicLong

/**
 * CarPlayManager — Entry point chính của AACP SDK
 *
 * Sử dụng:
 * ```kotlin
 * val manager = CarPlayManager(context)
 * manager.listener = object : CarPlayManager.Listener { ... }
 * manager.initialize(surface, certPath, keyPath)
 * manager.start()
 * // Khi xong:
 * manager.release()
 * ```
 */
class CarPlayManager(private val context: Context) {

    companion object {
        private const val TAG = "CarPlayManager"

        // Load native library — libaacp_jni.so (chứa cả aacp_core)
        init {
            System.loadLibrary("aacp_jni")
        }

        // AacpState values — phải match với Types.h
        const val STATE_DISCONNECTED   = 0
        const val STATE_CONNECTING     = 1
        const val STATE_AUTHENTICATING = 2
        const val STATE_CONNECTED      = 3
        const val STATE_ERROR          = 4

        // AacpError values
        const val ERROR_OK             = 0
        const val ERROR_USB            = -1
        const val ERROR_PROTOCOL       = -2
        const val ERROR_SSL            = -3
        const val ERROR_AUTH           = -4
        const val ERROR_TIMEOUT        = -5
    }

    // ── Listener interface ────────────────────────────────────────────────────

    interface Listener {
        /** Gọi khi CarPlay session active (sau khi auth xong) */
        fun onConnected()

        /** Gọi khi bị disconnect (USB rút, iPhone lock screen, etc.) */
        fun onDisconnected()

        /**
         * Gọi khi có lỗi.
         * @param errorCode  Một trong các CarPlayManager.ERROR_* constants
         * @param message    Mô tả lỗi human-readable
         */
        fun onError(errorCode: Int, message: String)

        /** Gọi khi state thay đổi (để debug UI) */
        fun onStateChanged(state: Int) {}
    }

    // ── Properties ────────────────────────────────────────────────────────────

    var listener: Listener? = null

    private val nativeHandle = AtomicLong(0L)
    private var videoDecoder: VideoDecoder? = null
    private var audioPlayer: AudioPlayer? = null
    private var micCapture: MicCapture? = null
    private var touchForwarder: TouchForwarder? = null
    private var initialized = false

    // ── Public API ────────────────────────────────────────────────────────────

    /**
     * Khởi tạo SDK với Surface để render video CarPlay.
     *
     * @param surface    SurfaceView.holder.surface — CarPlay video render vào đây
     * @param certPath   Đường dẫn đến MFi certificate PEM (null nếu dev mode)
     * @param keyPath    Đường dẫn đến private key PEM (null nếu dev mode)
     * @return true nếu khởi tạo thành công
     */
    fun initialize(
        surface: Surface,
        certPath: String? = null,
        keyPath: String? = null
    ): Boolean {
        if (initialized) {
            Log.w(TAG, "Already initialized — call release() first")
            return false
        }

        // Khởi tạo media pipeline
        videoDecoder = VideoDecoder(surface).also { it.initialize() }
        audioPlayer  = AudioPlayer().also { it.initialize() }
        touchForwarder = TouchForwarder { bytes ->
            val h = nativeHandle.get()
            if (h != 0L) nativeSendTouch(h, bytes)
        }

        // Tạo native context
        val handle = nativeCreate(certPath ?: "", keyPath ?: "")
        if (handle == 0L) {
            Log.e(TAG, "nativeCreate failed")
            releaseMediaPipeline()
            return false
        }
        nativeHandle.set(handle)

        // Đăng ký callbacks từ native → Kotlin
        setupNativeCallbacks(handle)

        initialized = true
        Log.i(TAG, "Initialized (handle=$handle, cert=${certPath != null})")
        return true
    }

    /**
     * Bắt đầu USB scan và kết nối.
     * Kết quả trả về qua Listener callbacks (non-blocking).
     */
    fun start(): Boolean {
        val h = nativeHandle.get()
        if (h == 0L) {
            Log.e(TAG, "start() called before initialize()")
            return false
        }
        return nativeStart(h)
    }

    /**
     * Dừng kết nối và giải phóng tất cả resources.
     * An toàn để gọi nhiều lần.
     */
    fun release() {
        val h = nativeHandle.getAndSet(0L)
        if (h != 0L) {
            nativeStop(h)
            nativeDestroy(h)
        }
        micCapture?.stop()
        micCapture = null
        releaseMediaPipeline()
        initialized = false
        Log.i(TAG, "Released")
    }

    /**
     * Forward touch event từ SurfaceView lên iPhone.
     * Gọi trong onTouchEvent() của Activity/Fragment.
     */
    fun onTouchEvent(event: MotionEvent): Boolean {
        return touchForwarder?.onTouchEvent(event) ?: false
    }

    /** Lấy trạng thái session hiện tại */
    fun getState(): Int {
        val h = nativeHandle.get()
        return if (h != 0L) nativeGetState(h) else STATE_DISCONNECTED
    }

    /** SDK version string */
    fun getSdkVersion(): String = "1.0.0"

    // ── Private helpers ───────────────────────────────────────────────────────

    private fun setupNativeCallbacks(handle: Long) {
        // Video callback: ByteArray + timestamp → VideoDecoder
        nativeSetVideoCallback(handle) { data: ByteArray, timestampUs: Long ->
            videoDecoder?.feedNalu(data, timestampUs)
        }

        // Audio callback: ByteArray + timestamp → AudioPlayer
        nativeSetAudioCallback(handle) { data: ByteArray, timestampUs: Long ->
            audioPlayer?.feedAacData(data, timestampUs)
        }

        // State callback: state + error → Listener
        nativeSetStateCallback(handle) { state: Int, error: Int ->
            handleStateChange(state, error)
        }
    }

    private fun handleStateChange(state: Int, error: Int) {
        // Callback từ native thread → switch sang main thread
        android.os.Handler(android.os.Looper.getMainLooper()).post {
            listener?.onStateChanged(state)
            when (state) {
                STATE_CONNECTED -> {
                    Log.i(TAG, "CarPlay connected!")
                    startMicCapture()
                    listener?.onConnected()
                }
                STATE_DISCONNECTED -> {
                    Log.i(TAG, "CarPlay disconnected")
                    micCapture?.stop()
                    listener?.onDisconnected()
                }
                STATE_ERROR -> {
                    val msg = errorMessage(error)
                    Log.e(TAG, "CarPlay error: $msg (code=$error)")
                    listener?.onError(error, msg)
                }
                else -> Log.d(TAG, "State changed: $state")
            }
        }
    }

    private fun startMicCapture() {
        micCapture?.stop()
        micCapture = MicCapture { pcmData ->
            val h = nativeHandle.get()
            if (h != 0L) nativeSendMic(h, pcmData)
        }.also { it.start() }
    }

    private fun releaseMediaPipeline() {
        videoDecoder?.release(); videoDecoder = null
        audioPlayer?.release();  audioPlayer  = null
    }

    private fun errorMessage(code: Int): String = when (code) {
        ERROR_USB      -> "USB connection error"
        ERROR_PROTOCOL -> "Protocol parse error"
        ERROR_SSL      -> "TLS handshake failed"
        ERROR_AUTH     -> "MFi authentication failed (no certificate)"
        ERROR_TIMEOUT  -> "Connection timeout"
        else           -> "Unknown error ($code)"
    }

    // ── JNI declarations ──────────────────────────────────────────────────────
    // Tên phải match chính xác với Java_com_aacp_sdk_CarPlayManager_* trong jni_bridge.cpp

    private external fun nativeCreate(certPath: String, keyPath: String): Long
    private external fun nativeStart(handle: Long): Boolean
    private external fun nativeStop(handle: Long)
    private external fun nativeDestroy(handle: Long)

    private external fun nativeSetVideoCallback(
        handle: Long,
        callback: (ByteArray, Long) -> Unit
    )
    private external fun nativeSetAudioCallback(
        handle: Long,
        callback: (ByteArray, Long) -> Unit
    )
    private external fun nativeSetStateCallback(
        handle: Long,
        callback: (Int, Int) -> Unit
    )

    private external fun nativeSendTouch(handle: Long, data: ByteArray): Boolean
    private external fun nativeSendMic(handle: Long, data: ByteArray): Boolean
    private external fun nativeGetState(handle: Long): Int
}
