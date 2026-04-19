package com.aacp.sdk.input

import android.view.MotionEvent
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * TouchForwarder — Serialize Android MotionEvent thành binary packet để gửi lên iPhone.
 *
 * Binary packet format (big-endian):
 * ┌─────────────────────────────────────────────────────┐
 * │ Header (6 bytes)                                    │
 * │  [4B] timestamp_ms  — System.currentTimeMillis()   │
 * │  [1B] action        — 1=DOWN, 2=MOVE, 3=UP, 4=CANCEL│
 * │  [1B] pointer_count — số ngón tay (1..10)           │
 * ├─────────────────────────────────────────────────────┤
 * │ Per-pointer (9 bytes × pointer_count)               │
 * │  [1B] pointer_id    — 0..9                          │
 * │  [4B] x            — float, pixel coordinate        │
 * │  [4B] y            — float, pixel coordinate        │
 * └─────────────────────────────────────────────────────┘
 * Total: 6 + (9 × pointer_count) bytes
 *
 * Coordinate system: pixels tính từ góc trên-trái của CarPlay SurfaceView.
 * iPhone sẽ scale về logical CarPlay coordinate space (800×480 or 1280×720).
 */
class TouchForwarder(
    private val onPacket: (ByteArray) -> Unit
) {

    // Action codes — phải match với iAP2 touch protocol
    companion object {
        const val ACTION_DOWN   = 1.toByte()
        const val ACTION_MOVE   = 2.toByte()
        const val ACTION_UP     = 3.toByte()
        const val ACTION_CANCEL = 4.toByte()

        // Header size = 4 (ts) + 1 (action) + 1 (count) = 6
        private const val HEADER_SIZE   = 6
        // Per-pointer = 1 (id) + 4 (x float) + 4 (y float) = 9
        private const val POINTER_SIZE  = 9
        private const val MAX_POINTERS = 10
    }

    /**
     * Gọi trong onTouchEvent() của Activity hoặc View.
     * @return true nếu event được xử lý và forward thành công
     */
    fun onTouchEvent(event: MotionEvent): Boolean {
        val action = mapAction(event.actionMasked) ?: return false

        val pointerCount = minOf(event.pointerCount, MAX_POINTERS)
        val packetSize   = HEADER_SIZE + (POINTER_SIZE * pointerCount)

        val buf = ByteBuffer.allocate(packetSize)
            .order(ByteOrder.BIG_ENDIAN)

        // Header
        val ts = (System.currentTimeMillis() and 0xFFFFFFFFL).toInt()
        buf.putInt(ts)
        buf.put(action)
        buf.put(pointerCount.toByte())

        // Per-pointer data
        for (i in 0 until pointerCount) {
            buf.put(event.getPointerId(i).toByte())
            buf.putFloat(event.getX(i))
            buf.putFloat(event.getY(i))
        }

        onPacket(buf.array())
        return true
    }

    private fun mapAction(androidAction: Int): Byte? = when (androidAction) {
        MotionEvent.ACTION_DOWN,
        MotionEvent.ACTION_POINTER_DOWN -> ACTION_DOWN
        MotionEvent.ACTION_MOVE         -> ACTION_MOVE
        MotionEvent.ACTION_UP,
        MotionEvent.ACTION_POINTER_UP   -> ACTION_UP
        MotionEvent.ACTION_CANCEL       -> ACTION_CANCEL
        else                            -> null
    }
}
