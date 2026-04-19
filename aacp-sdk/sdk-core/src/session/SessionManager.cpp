// ─────────────────────────────────────────────────────────────────────────────
// SessionManager.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "SessionManager.h"
#include <cstring>

#ifdef ANDROID
    #include <android/log.h>
    #define TAG "AACP_Session"
    #define LOGI(fmt,...) __android_log_print(ANDROID_LOG_INFO,  TAG, fmt, ##__VA_ARGS__)
    #define LOGW(fmt,...) __android_log_print(ANDROID_LOG_WARN,  TAG, fmt, ##__VA_ARGS__)
    #define LOGE(fmt,...) __android_log_print(ANDROID_LOG_ERROR, TAG, fmt, ##__VA_ARGS__)
#else
    #include <cstdio>
    #define LOGI(fmt,...) fprintf(stdout, "[SESSION INFO] " fmt "\n", ##__VA_ARGS__)
    #define LOGW(fmt,...) fprintf(stdout, "[SESSION WARN] " fmt "\n", ##__VA_ARGS__)
    #define LOGE(fmt,...) fprintf(stderr, "[SESSION ERR ] " fmt "\n", ##__VA_ARGS__)
#endif

namespace aacp {

SessionManager::SessionManager(AacpProtocol& protocol, SendFn sendFn)
    : protocol_(protocol), sendFn_(std::move(sendFn)) {

    // Đăng ký handler cho control channel
    protocol_.setHandler(Channel::Control,
        [this](const AacpFrame& f) { onControlFrame(f); });
}

SessionManager::~SessionManager() {
    if (ssl_)    { SSL_free(ssl_); ssl_ = nullptr; }
    if (sslCtx_) { SSL_CTX_free(sslCtx_); sslCtx_ = nullptr; }
    // readBio_ và writeBio_ được SSL_free() giải phóng cùng ssl_
}

// ── Initialize ────────────────────────────────────────────────────────────────

bool SessionManager::initialize(const std::string& certPath, const std::string& keyPath) {
    // Init OpenSSL (idempotent trong OpenSSL 1.1+)
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    // TLS 1.2 — CarPlay không dùng TLS 1.3
    sslCtx_ = SSL_CTX_new(TLS_client_method());
    if (!sslCtx_) {
        LOGE("SSL_CTX_new failed: %s", sslErrorString().c_str());
        return false;
    }

    SSL_CTX_set_min_proto_version(sslCtx_, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(sslCtx_, TLS1_2_VERSION);

    // Cipher suites CarPlay chấp nhận
    SSL_CTX_set_cipher_list(sslCtx_,
        "ECDHE-RSA-AES128-GCM-SHA256:"
        "ECDHE-RSA-AES256-GCM-SHA384:"
        "AES128-GCM-SHA256"
    );

    // Load client certificate (cần MFi cert cho production)
    if (!certPath.empty() && !keyPath.empty()) {
        if (SSL_CTX_use_certificate_file(sslCtx_, certPath.c_str(), SSL_FILETYPE_PEM) != 1) {
            LOGW("Cannot load cert '%s' — continuing without client cert", certPath.c_str());
        }
        if (SSL_CTX_use_PrivateKey_file(sslCtx_, keyPath.c_str(), SSL_FILETYPE_PEM) != 1) {
            LOGW("Cannot load key '%s'", keyPath.c_str());
        }
    }

    // Development: bỏ qua verify server cert
    // Production: SSL_CTX_set_verify(sslCtx_, SSL_VERIFY_PEER, nullptr);
    SSL_CTX_set_verify(sslCtx_, SSL_VERIFY_NONE, nullptr);

    // Tạo SSL object với memory BIOs
    // Memory BIO: data không đi qua socket mà đi qua USB channel
    ssl_      = SSL_new(sslCtx_);
    readBio_  = BIO_new(BIO_s_mem());
    writeBio_ = BIO_new(BIO_s_mem());

    // ssl_ sở hữu cả hai BIO — sẽ free khi SSL_free()
    SSL_set_bio(ssl_, readBio_, writeBio_);
    SSL_set_connect_state(ssl_);  // head unit là TLS client

    LOGI("SSL context initialized (TLS 1.2)");
    return true;
}

// ── Handshake kick-off ────────────────────────────────────────────────────────

void SessionManager::startHandshake() {
    LOGI("Starting version negotiation");
    transitionTo(AACP_STATE_CONNECTING);

    // VersionRequest payload: [major=1][minor=0][min_major=1][min_minor=0]
    std::vector<uint8_t> payload = {
        0x00, 0x01,   // major version
        0x00, 0x00,   // minor version
        0x00, 0x01,   // minimum major
        0x00, 0x00,   // minimum minor
    };
    sendControlFrame(ControlMsg::VersionRequest, payload);
}

// ── Control frame dispatcher ──────────────────────────────────────────────────

void SessionManager::onControlFrame(const AacpFrame& frame) {
    if (frame.payload.size() < 2) return;

    uint16_t msgType = ((uint16_t)frame.payload[0] << 8) | frame.payload[1];
    std::vector<uint8_t> body(frame.payload.begin() + 2, frame.payload.end());

    auto type = static_cast<ControlMsg>(msgType);
    switch (type) {
        case ControlMsg::VersionResponse:
            handleVersionResponse(body);
            break;
        case ControlMsg::SslHandshake:
            handleSslData(body);
            break;
        case ControlMsg::AuthComplete:
            handleAuthComplete();
            break;
        case ControlMsg::Ping:
            // Respond với Pong ngay lập tức
            sendControlFrame(ControlMsg::Pong, body);
            break;
        default:
            LOGW("Unhandled control msg: 0x%04x", msgType);
    }
}

// ── Version negotiation ───────────────────────────────────────────────────────

void SessionManager::handleVersionResponse(const std::vector<uint8_t>& payload) {
    if (payload.size() < 4) {
        LOGE("VersionResponse too short: %zu bytes", payload.size());
        return;
    }
    peerMajor_ = ((uint16_t)payload[0] << 8) | payload[1];
    peerMinor_ = ((uint16_t)payload[2] << 8) | payload[3];
    LOGI("Peer version: %u.%u", peerMajor_, peerMinor_);

    transitionTo(AACP_STATE_AUTHENTICATING);

    // Bắt đầu TLS handshake: gửi ClientHello
    std::lock_guard<std::mutex> lock(sslMutex_);
    tickSsl();
}

// ── TLS handshake qua USB channel ─────────────────────────────────────────────

void SessionManager::handleSslData(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(sslMutex_);

    // Đẩy bytes từ iPhone vào read BIO (SSL sẽ consume khi tickSsl() được gọi)
    BIO_write(readBio_, data.data(), (int)data.size());
    tickSsl();
}

void SessionManager::tickSsl() {
    // sslMutex_ phải đã held bởi caller

    if (sslHandshakeComplete()) {
        // Handshake đã xong — flush bất kỳ pending data
        // (e.g., session tickets, NewSessionTicket)
    } else {
        // Tiếp tục handshake
        int ret = SSL_do_handshake(ssl_);
        int err = SSL_get_error(ssl_, ret);

        if (err != SSL_ERROR_NONE &&
            err != SSL_ERROR_WANT_READ &&
            err != SSL_ERROR_WANT_WRITE) {
            LOGE("SSL_do_handshake error: %d — %s", err, sslErrorString().c_str());
            transitionTo(AACP_STATE_ERROR, AACP_ERROR_SSL);
            return;
        }

        if (SSL_is_init_finished(ssl_)) {
            LOGI("TLS handshake complete — cipher: %s", SSL_get_cipher_name(ssl_));
            // NOTE: Ở đây thực tế iPhone sẽ verify MFi cert
            // Nếu không có → iPhone drop connection → AACP_STATE_ERROR
            // Với development: tiếp tục như thể auth thành công
        }
    }

    // Flush writeBio → gửi qua USB (SSL output: ClientHello, CertVerify, etc.)
    size_t pending = (size_t)BIO_ctrl_pending(writeBio_);
    if (pending > 0) {
        std::vector<uint8_t> sslOut(pending);
        int read = BIO_read(writeBio_, sslOut.data(), (int)pending);
        if (read > 0) {
            sslOut.resize(read);
            sendControlFrame(ControlMsg::SslHandshake, sslOut);
        }
    }
}

bool SessionManager::sslHandshakeComplete() {
    return ssl_ && SSL_is_init_finished(ssl_);
}

// ── Auth complete ─────────────────────────────────────────────────────────────

void SessionManager::handleAuthComplete() {
    LOGI("Authentication complete — CarPlay session active");
    transitionTo(AACP_STATE_CONNECTED, AACP_OK);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void SessionManager::sendControlFrame(ControlMsg type,
                                       const std::vector<uint8_t>& payload) {
    AacpFrame frame = AacpProtocol::makeControlFrame(type, payload);
    auto bytes = AacpProtocol::serialize(frame);
    if (!sendFn_(bytes.data(), bytes.size())) {
        LOGE("sendControlFrame failed — USB write error");
    }
}

bool SessionManager::sendRaw(const std::vector<uint8_t>& bytes) {
    return sendFn_(bytes.data(), bytes.size());
}

void SessionManager::transitionTo(AacpState state, AacpError err) {
    AacpState old = state_.exchange(state, std::memory_order_acq_rel);
    if (old != state) {
        const char* names[] = {"DISCONNECTED","CONNECTING","AUTHENTICATING","CONNECTED","ERROR"};
        LOGI("State: %s → %s", names[old], names[state]);
        if (stateCallback_) stateCallback_(state, err);
    }
}

std::string SessionManager::sslErrorString() {
    char buf[256];
    ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
    return std::string(buf);
}

} // namespace aacp
