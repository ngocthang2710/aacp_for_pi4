#!/usr/bin/env bash
# =============================================================================
# verify_layers.sh — Kiểm tra từng layer của AACP SDK
#
# Chạy lần lượt:
#   1. Layer 0: Môi trường (tools, NDK, deps)
#   2. Layer 1: USB Transport (lsusb, AOA detection)
#   3. Layer 2: Protocol Parser (mock data test)
#   4. Layer 3: .so symbols (export đúng không)
#   5. Layer 4: ADB logcat filter (khi chạy trên device)
#
# Usage: ./verify_layers.sh [--layer=N] [--device]
# =============================================================================
set -uo pipefail

# ── Colors & helpers ──────────────────────────────────────────────────────────
GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

pass()  { echo -e "  ${GREEN}✓${NC} $*"; }
fail()  { echo -e "  ${RED}✗${NC} $*"; ((FAIL_COUNT++)) || true; }
warn()  { echo -e "  ${YELLOW}⚠${NC} $*"; }
head()  { echo -e "\n${BOLD}${CYAN}━━ $* ━━${NC}"; }

FAIL_COUNT=0
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SO_PATH="$PROJECT_ROOT/outputs/jniLibs/arm64-v8a/libaacp_core.so"

# ── Layer 0: Environment ──────────────────────────────────────────────────────
verify_env() {
    head "Layer 0: Environment"

    # cmake
    if command -v cmake &>/dev/null; then
        pass "cmake $(cmake --version | head -1 | grep -o '[0-9\.]*' | head -1)"
    else
        fail "cmake not found. Install: sudo apt install cmake"
    fi

    # libusb
    if pkg-config --exists libusb-1.0 2>/dev/null; then
        pass "libusb-1.0 $(pkg-config --modversion libusb-1.0)"
    else
        fail "libusb-1.0 not found. Install: sudo apt install libusb-1.0-0-dev"
    fi

    # openssl
    if pkg-config --exists openssl 2>/dev/null; then
        pass "openssl $(pkg-config --modversion openssl)"
    else
        fail "openssl not found. Install: sudo apt install libssl-dev"
    fi

    # NDK
    NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK:-}}"
    if [[ -n "$NDK" && -d "$NDK" ]]; then
        NDK_VER=$(cat "$NDK/source.properties" 2>/dev/null | grep Revision | cut -d= -f2 | xargs)
        pass "Android NDK r${NDK_VER:-?} at $NDK"
    else
        warn "ANDROID_NDK_HOME not set — .so build will fail"
    fi

    # adb (optional)
    if command -v adb &>/dev/null; then
        pass "adb $(adb version | head -1 | grep -o '[0-9\.]*' | head -1)"
    else
        warn "adb not found — device verification skipped"
    fi

    # ninja (optional but faster)
    if command -v ninja &>/dev/null; then
        pass "ninja $(ninja --version)"
    else
        warn "ninja not found — cmake will use make (slower)"
    fi
}

# ── Layer 1: USB Detection ────────────────────────────────────────────────────
verify_usb() {
    head "Layer 1: USB Transport"

    if ! command -v lsusb &>/dev/null; then
        warn "lsusb not found. Install: sudo apt install usbutils"
        return
    fi

    echo "  Scanning USB bus for Apple devices..."
    APPLE_DEVICES=$(lsusb | grep -i "Apple\|05ac" || true)

    if [[ -z "$APPLE_DEVICES" ]]; then
        warn "No Apple device detected on USB"
        echo "  → Cắm iPhone vào Pi4 bằng USB cable và chạy lại"
    else
        echo "$APPLE_DEVICES" | while read -r line; do
            PID=$(echo "$line" | grep -o '05ac:[0-9a-f]*' | cut -d: -f2)
            # AOA mode PIDs: 2d00 (AOA 1.0) hoặc 2d01 (AOA 2.0 with audio)
            case "$PID" in
                2d00|2d01)
                    pass "Apple device in AOA mode: $line"
                    pass "USB Transport layer: WORKING"
                    ;;
                *)
                    pass "Apple device detected: $line"
                    warn "Device not in AOA mode yet (PID=0x$PID)"
                    echo "    → Chạy SDK để trigger AOA switch, sau đó check lại"
                    ;;
            esac
        done
    fi

    # Check usbmon (cần để capture với Wireshark)
    if lsmod 2>/dev/null | grep -q usbmon; then
        pass "usbmon kernel module loaded (Wireshark capture available)"
    else
        warn "usbmon not loaded. To capture USB: sudo modprobe usbmon"
    fi
}

# ── Layer 2: Protocol Parser ──────────────────────────────────────────────────
verify_protocol() {
    head "Layer 2: Protocol Parser"

    # Build native test binary nếu có
    TEST_BIN="$PROJECT_ROOT/build_native_test/protocol_test"
    if [[ -f "$TEST_BIN" ]]; then
        pass "Protocol test binary found"
        if "$TEST_BIN" 2>&1 | grep -q "PASS"; then
            pass "Protocol parser: all tests PASS"
        else
            fail "Protocol parser: some tests FAILED"
            "$TEST_BIN" 2>&1 | grep -E "PASS|FAIL|Error"
        fi
    else
        warn "Protocol test binary not built"
        echo "  → Build tests: cmake -B build_native_test -DAACP_BUILD_TESTS=ON && cmake --build build_native_test"
    fi

    # Verify frame format với python (luôn available)
    if command -v python3 &>/dev/null; then
        echo "  Running Python protocol frame test..."
        python3 - <<'PYEOF'
import struct, sys

# AACP frame format: [4B length][2B channel][2B flags][payload]
def make_frame(channel, flags, payload):
    total = 8 + len(payload)
    return struct.pack('>IHH', total, channel, flags) + payload

def parse_frame(data):
    if len(data) < 8:
        return None, data
    total, ch, flags = struct.unpack('>IHH', data[:8])
    if len(data) < total:
        return None, data
    payload = data[8:total]
    return {'total': total, 'channel': ch, 'flags': flags, 'payload': payload}, data[total:]

# Test 1: basic frame
frame = make_frame(0, 0, b'hello')
parsed, rest = parse_frame(frame)
assert parsed is not None
assert parsed['channel'] == 0
assert parsed['payload'] == b'hello'
assert len(rest) == 0
print("  ✓ Test 1: Basic frame parse")

# Test 2: multiple frames concatenated
f1 = make_frame(1, 0x0001, b'\x00\x00\x00\x01' + b'H264')  # video keyframe
f2 = make_frame(2, 0, b'\x12\x10' + bytes(100))             # audio
buf = f1 + f2
p1, buf = parse_frame(buf)
p2, buf = parse_frame(buf)
assert p1['channel'] == 1 and p1['flags'] == 0x0001
assert p2['channel'] == 2
assert len(buf) == 0
print("  ✓ Test 2: Multi-frame stream parse")

# Test 3: incomplete frame (split across USB packets)
frame = make_frame(0, 0, b'world')
half = frame[:5]
p, rest = parse_frame(half)
assert p is None and rest == half  # chưa đủ data
print("  ✓ Test 3: Incomplete frame handled correctly")

# Test 4: control message sub-protocol
def make_control(msg_type, payload=b''):
    ctrl_payload = struct.pack('>H', msg_type) + payload
    return make_frame(0, 0, ctrl_payload)

version_req = make_control(0x0001, struct.pack('>HHHH', 1, 0, 1, 0))
p, _ = parse_frame(version_req)
msg_type = struct.unpack('>H', p['payload'][:2])[0]
assert msg_type == 0x0001
print("  ✓ Test 4: Control message (VersionRequest)")

print("\n  Protocol layer: ALL TESTS PASSED")
PYEOF
        if [[ $? -eq 0 ]]; then
            pass "Python protocol tests: PASSED"
        else
            fail "Python protocol tests: FAILED"
        fi
    fi
}

# ── Layer 3: .so Symbols ──────────────────────────────────────────────────────
verify_so() {
    head "Layer 3: Shared Library (.so)"

    if [[ ! -f "$SO_PATH" ]]; then
        warn ".so not built yet: $SO_PATH"
        echo "  → Run: ./scripts/build_so.sh"
        return
    fi

    SO_SIZE=$(du -sh "$SO_PATH" | cut -f1)
    pass "libaacp_core.so exists ($SO_SIZE)"

    # Check file type
    FILE_INFO=$(file "$SO_PATH" 2>/dev/null || echo "")
    if echo "$FILE_INFO" | grep -q "aarch64"; then
        pass "Architecture: ARM64 (aarch64) ✓"
    elif echo "$FILE_INFO" | grep -q "ELF"; then
        pass "ELF format: $FILE_INFO"
    else
        warn "Cannot verify architecture (install 'file' command)"
    fi

    # Check exported symbols with nm or readelf
    NM_CMD=""
    if command -v nm &>/dev/null; then NM_CMD="nm"
    elif command -v aarch64-linux-android-nm &>/dev/null; then NM_CMD="aarch64-linux-android-nm"
    fi

    EXPECTED_SYMBOLS=(
        "aacp_create"
        "aacp_start"
        "aacp_stop"
        "aacp_destroy"
        "aacp_set_video_callback"
        "aacp_set_audio_callback"
        "aacp_set_state_callback"
        "aacp_send_touch"
        "aacp_send_mic"
        "aacp_get_state"
        "aacp_version"
    )

    if [[ -n "$NM_CMD" ]]; then
        echo "  Checking exported symbols:"
        ALL_PASS=true
        for sym in "${EXPECTED_SYMBOLS[@]}"; do
            if "$NM_CMD" -D "$SO_PATH" 2>/dev/null | grep -q " T $sym"; then
                pass "$sym"
            else
                fail "$sym NOT found in exports"
                ALL_PASS=false
            fi
        done
        $ALL_PASS && pass "All expected symbols exported correctly"
    else
        warn "nm not found — cannot verify symbols"
        echo "  Install: sudo apt install binutils"
    fi

    # Check no undefined symbols (except system libs)
    if [[ -n "$NM_CMD" ]]; then
        UNDEFINED=$("$NM_CMD" -D "$SO_PATH" 2>/dev/null | grep " U " | \
            grep -v "liblog\|libandroid\|libc\|libdl\|libm\|libstdc\|__" | \
            head -5)
        if [[ -z "$UNDEFINED" ]]; then
            pass "No unexpected undefined symbols"
        else
            warn "Undefined symbols (may cause runtime crash):"
            echo "$UNDEFINED" | while read -r line; do echo "    $line"; done
        fi
    fi
}

# ── Layer 4: Device Logcat ────────────────────────────────────────────────────
verify_device() {
    head "Layer 4: On-Device Verification (ADB Logcat)"

    if ! command -v adb &>/dev/null; then
        warn "adb not found — skip device verification"
        return
    fi

    DEVICE=$(adb devices 2>/dev/null | grep -v "List\|^$" | head -1 | cut -f1)
    if [[ -z "$DEVICE" ]]; then
        warn "No ADB device connected"
        echo "  → Kết nối Pi4 qua ADB (adb connect <pi4-ip>:5555)"
        return
    fi
    pass "ADB device: $DEVICE"

    echo "  Monitoring logcat for AACP tags (10 seconds)..."
    echo "  ┌─────────────────────────────────────────────────────┐"
    timeout 10 adb -s "$DEVICE" logcat \
        -s "AACP_USB:*" "AACP_PROTO:*" "AACP_Session:*" "AACP_JNI:*" \
           "CarPlayManager:*" "AACP_Video:*" "AACP_Audio:*" "AACP_Mic:*" \
        --format=brief 2>/dev/null | \
        while read -r line; do
            echo "  │ $line"
        done || true
    echo "  └─────────────────────────────────────────────────────┘"

    echo ""
    echo "  Expected log sequence khi iPhone cắm vào:"
    echo "    [AACP_USB  ] Found Apple device: VID=0x05ac PID=0x..."
    echo "    [AACP_USB  ] AOA protocol version: 2"
    echo "    [AACP_USB  ] Switching to AOA mode, waiting for re-enumerate..."
    echo "    [AACP_USB  ] Device already in AOA mode (PID=0x2d01)"
    echo "    [AACP_USB  ] USB transport started"
    echo "    [AACP_Session] Version request sent"
    echo "    [AACP_Session] Peer version: 1.0"
    echo "    [AACP_Session] TLS handshake complete"
    echo "    [AACP_Session] State: CONNECTING → AUTHENTICATING"
    echo "    *** AUTH FAILS HERE without MFi cert ***"
    echo "    [AACP_Session] State: AUTHENTICATING → ERROR (AACP_ERROR_AUTH)"
}

# ── Summary ───────────────────────────────────────────────────────────────────
print_summary() {
    echo ""
    echo -e "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    if [[ $FAIL_COUNT -eq 0 ]]; then
        echo -e "${GREEN}${BOLD}  All checks passed!${NC}"
    else
        echo -e "${RED}${BOLD}  $FAIL_COUNT check(s) failed${NC}"
        echo "  Fix the issues above and run again"
    fi
    echo -e "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}

# ── Main ──────────────────────────────────────────────────────────────────────
LAYER="${1:-all}"

case "$LAYER" in
    --layer=0|0) verify_env ;;
    --layer=1|1) verify_usb ;;
    --layer=2|2) verify_protocol ;;
    --layer=3|3) verify_so ;;
    --layer=4|4) verify_device ;;
    *)
        verify_env
        verify_usb
        verify_protocol
        verify_so
        verify_device
        ;;
esac

print_summary
exit $FAIL_COUNT
