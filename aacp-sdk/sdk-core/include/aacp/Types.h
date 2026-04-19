#pragma once
#include <cstdint>
#include <cstddef>

// ── Visibility macro ─────────────────────────────────────────────────────────
// Chỉ các symbol được đánh dấu AACP_API mới được export ra .so
#if defined(_WIN32)
    #define AACP_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
    #define AACP_API __attribute__((visibility("default")))
#else
    #define AACP_API
#endif

// ── Handle type ───────────────────────────────────────────────────────────────
// Opaque pointer — consumer không biết cấu trúc bên trong
typedef long AacpHandle;
#define AACP_INVALID_HANDLE  ((AacpHandle)0)

// ── Session states ────────────────────────────────────────────────────────────
typedef enum {
    AACP_STATE_DISCONNECTED  = 0,  // Không có kết nối
    AACP_STATE_CONNECTING    = 1,  // Đang AOA handshake / USB init
    AACP_STATE_AUTHENTICATING= 2,  // Đang TLS + iAP2 auth
    AACP_STATE_CONNECTED     = 3,  // Session active — CarPlay running
    AACP_STATE_ERROR         = 4,  // Lỗi không recover được
} AacpState;

// ── Error codes ───────────────────────────────────────────────────────────────
typedef enum {
    AACP_OK                  =  0,
    AACP_ERROR_USB           = -1,   // USB transport error
    AACP_ERROR_PROTOCOL      = -2,   // Frame parse error
    AACP_ERROR_SSL           = -3,   // TLS handshake error
    AACP_ERROR_AUTH          = -4,   // MFi auth failed
    AACP_ERROR_TIMEOUT       = -5,   // Operation timeout
    AACP_ERROR_INVALID_PARAM = -6,   // NULL / bad argument
    AACP_ERROR_NOT_INIT      = -7,   // Chưa gọi aacp_create()
} AacpError;

// ── Video/Audio frame info ────────────────────────────────────────────────────
typedef struct {
    const uint8_t* data;         // Raw NALU bytes (Annex-B, có start code)
    int            size;
    int64_t        timestamp_us; // Presentation timestamp (microseconds)
    int            is_key_frame; // 1 nếu là IDR frame
} AacpVideoFrame;

typedef struct {
    const uint8_t* data;         // AAC-LC ADTS bytes
    int            size;
    int64_t        timestamp_us;
} AacpAudioFrame;

// ── Mic audio format ──────────────────────────────────────────────────────────
// SDK expect PCM 16-bit signed, 16000 Hz, mono
#define AACP_MIC_SAMPLE_RATE    16000
#define AACP_MIC_CHANNELS       1
#define AACP_MIC_BITS_PER_SAMPLE 16
