#pragma once

/**
 * AACP SDK — Public C API
 *
 * Usage flow:
 *   1. aacp_create()           → lấy handle
 *   2. aacp_set_*_callback()   → đăng ký callbacks
 *   3. aacp_start()            → bắt đầu USB scan + connect
 *   4. [callbacks fired...]
 *   5. aacp_stop()             → dừng I/O, disconnect
 *   6. aacp_destroy()          → giải phóng bộ nhớ
 *
 * Thread safety:
 *   - Tất cả API calls phải từ cùng 1 thread (hoặc serialize externally)
 *   - Callbacks được gọi từ internal threads — caller tự sync nếu cần
 */

#include "Types.h"
#include "Callbacks.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Lifecycle ─────────────────────────────────────────────────────────────────

/**
 * Tạo SDK instance.
 * @param cert_path  Path đến PEM certificate (NULL để bỏ qua TLS client cert)
 * @param key_path   Path đến PEM private key  (NULL để bỏ qua)
 * @return Handle hợp lệ, hoặc AACP_INVALID_HANDLE nếu thất bại
 */
AACP_API AacpHandle aacp_create(
    const char* cert_path,
    const char* key_path
);

/**
 * Bắt đầu USB scan, AOA handshake, và protocol loop.
 * Non-blocking — kết quả trả về qua state_callback.
 * @return AACP_OK hoặc AacpError
 */
AACP_API int aacp_start(AacpHandle handle);

/**
 * Dừng tất cả I/O và disconnect.
 * State callback sẽ được gọi với AACP_STATE_DISCONNECTED.
 */
AACP_API void aacp_stop(AacpHandle handle);

/**
 * Giải phóng toàn bộ tài nguyên. Phải gọi aacp_stop() trước.
 */
AACP_API void aacp_destroy(AacpHandle handle);

// ── Callbacks ─────────────────────────────────────────────────────────────────

AACP_API void aacp_set_video_callback(
    AacpHandle      handle,
    AacpVideoCallback cb,
    void*           userdata
);

AACP_API void aacp_set_audio_callback(
    AacpHandle      handle,
    AacpAudioCallback cb,
    void*           userdata
);

AACP_API void aacp_set_state_callback(
    AacpHandle      handle,
    AacpStateCallback cb,
    void*           userdata
);

/**
 * Optional: nhận log từ native layer để hiển thị trong app.
 * Nếu không set, log đi vào Android logcat với tag "AACP".
 */
AACP_API void aacp_set_log_callback(
    AacpHandle    handle,
    AacpLogCallback cb,
    void*         userdata
);

// ── Input: gửi data từ head unit lên iPhone ───────────────────────────────────

/**
 * Gửi touch event packet lên iPhone.
 * Format: xem TouchForwarder.kt để biết binary layout.
 * @return Số bytes đã gửi, hoặc AacpError < 0
 */
AACP_API int aacp_send_touch(
    AacpHandle    handle,
    const uint8_t* data,
    int            size
);

/**
 * Gửi microphone PCM data lên iPhone (cho Siri).
 * Format: PCM 16-bit signed, 16000 Hz, mono, big-endian.
 * @return Số bytes đã gửi, hoặc AacpError < 0
 */
AACP_API int aacp_send_mic(
    AacpHandle    handle,
    const uint8_t* data,
    int            size
);

/**
 * Gửi sensor data (GPS, speed, gear) lên iPhone.
 * @param sensor_type  0=GPS, 1=speed, 2=gear, 3=night_mode
 * @param data         Raw bytes theo format sensor_type
 */
AACP_API int aacp_send_sensor(
    AacpHandle    handle,
    int           sensor_type,
    const uint8_t* data,
    int            size
);

// ── Diagnostics ───────────────────────────────────────────────────────────────

/**
 * Lấy trạng thái hiện tại của session.
 */
AACP_API AacpState aacp_get_state(AacpHandle handle);

/**
 * Lấy version string của SDK.
 * @return String dạng "1.0.0" — không cần free
 */
AACP_API const char* aacp_version(void);

#ifdef __cplusplus
}
#endif
