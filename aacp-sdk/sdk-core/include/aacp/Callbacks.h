#pragma once
#include "Types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Callback function pointer types ──────────────────────────────────────────
// Tất cả callback đều được gọi từ background thread — KHÔNG block, KHÔNG UI call

/**
 * Gọi khi có H.264 NALU frame từ iPhone.
 * @param frame  Thông tin frame + raw bytes (valid chỉ trong scope callback)
 * @param userdata  Con trỏ do caller truyền vào aacp_set_video_callback()
 */
typedef void (*AacpVideoCallback)(
    const AacpVideoFrame* frame,
    void*                 userdata
);

/**
 * Gọi khi có AAC audio packet từ iPhone.
 */
typedef void (*AacpAudioCallback)(
    const AacpAudioFrame* frame,
    void*                 userdata
);

/**
 * Gọi khi trạng thái session thay đổi.
 * @param state  Giá trị AacpState mới
 * @param error  AACP_OK nếu không có lỗi, hoặc AacpError code
 */
typedef void (*AacpStateCallback)(
    AacpState state,
    AacpError error,
    void*     userdata
);

/**
 * Gọi khi có log message từ native layer.
 * @param level   0=DEBUG, 1=INFO, 2=WARN, 3=ERROR
 * @param tag     Module name, e.g. "USB", "PROTO", "SSL"
 * @param message Nội dung log (null-terminated)
 */
typedef void (*AacpLogCallback)(
    int         level,
    const char* tag,
    const char* message,
    void*       userdata
);

#ifdef __cplusplus
}
#endif
