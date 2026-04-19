package com.aacp.demo

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.util.Log
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.WindowManager
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.aacp.sdk.CarPlayManager

/**
 * CarPlayActivity — Demo activity sử dụng AACP SDK.
 *
 * Layout:
 *  - SurfaceView chiếm toàn màn hình (CarPlay video render ở đây)
 *  - TextView overlay ở góc trên-trái hiển thị connection state
 *  - Touch trên SurfaceView → forward lên iPhone
 */
class CarPlayActivity : AppCompatActivity(), SurfaceHolder.Callback {

    companion object {
        private const val TAG = "CarPlayActivity"
        private const val REQUEST_RECORD_AUDIO = 1001
    }

    private lateinit var surfaceView: SurfaceView
    private lateinit var statusText: TextView
    private lateinit var statsText: TextView

    private var carPlay: CarPlayManager? = null
    private var surfaceReady = false

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Full screen — không có status bar, navigation bar
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_FULLSCREEN or
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
            View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
            View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
        )

        setContentView(R.layout.activity_carplay)
        surfaceView = findViewById(R.id.surface_view)
        statusText  = findViewById(R.id.status_text)
        statsText   = findViewById(R.id.stats_text)

        surfaceView.holder.addCallback(this)
        surfaceView.keepScreenOn = true

        // Check mic permission
        checkMicPermission()
    }

    override fun onDestroy() {
        super.onDestroy()
        carPlay?.release()
        carPlay = null
    }

    // ── Permission ────────────────────────────────────────────────────────────

    private fun checkMicPermission() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
            != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(
                this,
                arrayOf(Manifest.permission.RECORD_AUDIO),
                REQUEST_RECORD_AUDIO
            )
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQUEST_RECORD_AUDIO) {
            if (grantResults.firstOrNull() != PackageManager.PERMISSION_GRANTED) {
                Toast.makeText(this, "Microphone permission required for Siri",
                    Toast.LENGTH_LONG).show()
            }
        }
    }

    // ── SurfaceHolder.Callback ────────────────────────────────────────────────

    override fun surfaceCreated(holder: SurfaceHolder) {
        Log.i(TAG, "Surface created")
        surfaceReady = true
        initAndStartCarPlay(holder)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        Log.i(TAG, "Surface changed: ${width}×${height}")
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        Log.i(TAG, "Surface destroyed")
        surfaceReady = false
        carPlay?.release()
        carPlay = null
    }

    // ── CarPlay Init ──────────────────────────────────────────────────────────

    private fun initAndStartCarPlay(holder: SurfaceHolder) {
        val manager = CarPlayManager(this)
        carPlay = manager

        // Cert paths — null trong dev mode (không có MFi cert)
        // Production: filesDir.absolutePath + "/carplay.crt"
        val certPath: String? = null
        val keyPath:  String? = null

        val ok = manager.initialize(holder.surface, certPath, keyPath)
        if (!ok) {
            showStatus("Init failed", error = true)
            return
        }

        // Listener
        manager.listener = object : CarPlayManager.Listener {
            override fun onConnected() {
                showStatus("CarPlay Connected ✓")
                startStatsUpdater()
            }
            override fun onDisconnected() {
                showStatus("Disconnected — plug in iPhone")
                stopStatsUpdater()
            }
            override fun onError(errorCode: Int, message: String) {
                showStatus("Error: $message", error = true)
                Log.e(TAG, "CarPlay error $errorCode: $message")
            }
            override fun onStateChanged(state: Int) {
                val label = when (state) {
                    CarPlayManager.STATE_DISCONNECTED   -> "Disconnected"
                    CarPlayManager.STATE_CONNECTING     -> "Connecting..."
                    CarPlayManager.STATE_AUTHENTICATING -> "Authenticating..."
                    CarPlayManager.STATE_CONNECTED      -> "Connected"
                    CarPlayManager.STATE_ERROR          -> "Error"
                    else                                -> "Unknown"
                }
                Log.d(TAG, "State: $label")
            }
        }

        showStatus("Scanning for iPhone...")
        manager.start()
    }

    // ── Touch forwarding ──────────────────────────────────────────────────────

    override fun onTouchEvent(event: MotionEvent): Boolean {
        // Forward tất cả touch events lên iPhone
        return carPlay?.onTouchEvent(event) ?: super.onTouchEvent(event)
    }

    // ── UI helpers ────────────────────────────────────────────────────────────

    private fun showStatus(msg: String, error: Boolean = false) {
        runOnUiThread {
            statusText.text = msg
            statusText.setTextColor(
                if (error) 0xFFFF4444.toInt() else 0xFFFFFFFF.toInt()
            )
        }
    }

    private var statsRunnable: Runnable? = null

    private fun startStatsUpdater() {
        statsRunnable = object : Runnable {
            override fun run() {
                val state = carPlay?.getState() ?: return
                statsText.text = "SDK v${carPlay?.getSdkVersion()} | State=$state"
                statsText.postDelayed(this, 1000)
            }
        }
        statsText.postDelayed(statsRunnable!!, 1000)
    }

    private fun stopStatsUpdater() {
        statsRunnable?.let { statsText.removeCallbacks(it) }
        statsRunnable = null
        runOnUiThread { statsText.text = "" }
    }
}
