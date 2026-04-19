#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SessionManager.h
// Quản lý vòng đời phiên CarPlay:
//   Version negotiation → TLS handshake → iAP2 auth → session active
// ─────────────────────────────────────────────────────────────────────────────
#include "../protocol/AacpProtocol.h"
#include "../../include/aacp/Types.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <functional>
#include <string>
#include <atomic>
#include <mutex>
#include <vector>

namespace aacp {

// Callback để SessionManager gửi data xuống USB
// (inject từ ngoài để tránh circular dependency)
using SendFn = std::function<bool(const uint8_t*, size_t)>;

class SessionManager {
public:
    using StateCallback = std::function<void(AacpState, AacpError)>;

    /**
     * @param protocol  AacpProtocol instance — SessionManager tự đăng ký handler
     * @param sendFn    Function để gửi bytes xuống USB (từ UsbTransport::send)
     */
    explicit SessionManager(AacpProtocol& protocol, SendFn sendFn);
    ~SessionManager();

    // Non-copyable
    SessionManager(const SessionManager&)            = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    /**
     * Khởi tạo OpenSSL context.
     * @param certPath  Path đến PEM cert (optional, NULL = no client cert)
     * @param keyPath   Path đến PEM key  (optional)
     */
    bool initialize(const std::string& certPath, const std::string& keyPath);

    /**
     * Bắt đầu handshake bằng cách gửi VersionRequest.
     * Phải gọi sau initialize() và sau khi USB connected.
     */
    void startHandshake();

    void setStateCallback(StateCallback cb) { stateCallback_ = std::move(cb); }
    AacpState getState() const { return state_.load(std::memory_order_acquire); }

private:
    // Control channel handlers
    void onControlFrame(const AacpFrame& frame);
    void handleVersionResponse(const std::vector<uint8_t>& payload);
    void handleSslData(const std::vector<uint8_t>& data);
    void handleAuthComplete();

    // SSL helpers
    void tickSsl();               // Chạy SSL state machine, flush write BIO
    bool sslHandshakeComplete();  // Kiểm tra handshake xong chưa

    // Send helpers
    void sendControlFrame(ControlMsg type, const std::vector<uint8_t>& payload = {});
    bool sendRaw(const std::vector<uint8_t>& bytes);

    void transitionTo(AacpState state, AacpError err = AACP_OK);
    std::string sslErrorString();

    AacpProtocol&  protocol_;
    SendFn         sendFn_;

    // OpenSSL objects
    SSL_CTX* sslCtx_   = nullptr;
    SSL*     ssl_      = nullptr;
    BIO*     readBio_  = nullptr;   // bytes từ iPhone → vào SSL
    BIO*     writeBio_ = nullptr;   // bytes SSL muốn gửi → lên iPhone

    std::atomic<AacpState> state_{AACP_STATE_DISCONNECTED};
    StateCallback          stateCallback_;
    std::mutex             sslMutex_;

    // CarPlay protocol version negotiated
    uint16_t peerMajor_ = 0;
    uint16_t peerMinor_ = 0;
};

} // namespace aacp
