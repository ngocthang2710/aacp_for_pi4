#!/usr/bin/env bash
# =============================================================================
# build_so.sh — Build libaacp_core.so cho Android ARM64 (Pi4)
#
# Output: outputs/jniLibs/arm64-v8a/libaacp_core.so
#
# Dùng cho:
#   - Tích hợp thẳng vào AOSP build system (Android.mk hoặc Android.bp)
#   - Test native layer trực tiếp không qua Gradle
#
# Yêu cầu:
#   - Android NDK (set ANDROID_NDK_HOME hoặc có trong PATH)
#   - cmake >= 3.22
#   - libusb-1.0-dev (cross-compiled cho arm64 — xem README)
#   - libssl-dev (cross-compiled cho arm64)
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ── Config ────────────────────────────────────────────────────────────────────
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK:-$HOME/Android/Sdk/ndk/27.0.12077973}}"
ABI="arm64-v8a"
API_LEVEL="29"
BUILD_TYPE="${1:-Release}"   # Usage: ./build_so.sh [Debug|Release]
BUILD_DIR="$PROJECT_ROOT/build_native_${ABI}_${BUILD_TYPE}"
OUTPUT_DIR="$PROJECT_ROOT/outputs/jniLibs/$ABI"

# ── Colors ────────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[build_so]${NC} $*"; }
warn()  { echo -e "${YELLOW}[build_so]${NC} $*"; }
error() { echo -e "${RED}[build_so] ERROR:${NC} $*" >&2; exit 1; }

# ── Validate NDK ──────────────────────────────────────────────────────────────
TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"
if [[ ! -f "$TOOLCHAIN" ]]; then
    error "NDK toolchain not found at: $TOOLCHAIN\nSet ANDROID_NDK_HOME correctly."
fi
info "Using NDK: $NDK"
info "ABI: $ABI | API: $API_LEVEL | Build: $BUILD_TYPE"

# ── CMake configure ───────────────────────────────────────────────────────────
mkdir -p "$BUILD_DIR"
cmake -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM="android-$API_LEVEL" \
    -DANDROID_STL="c++_shared" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DANDROID=TRUE \
    -DAACP_BUILD_TESTS=OFF \
    -S "$PROJECT_ROOT/sdk-core" \
    -G Ninja 2>&1 | grep -v "^--"

# ── Build ─────────────────────────────────────────────────────────────────────
info "Building..."
cmake --build "$BUILD_DIR" --target aacp_core -j"$(nproc)"

# ── Copy output ───────────────────────────────────────────────────────────────
mkdir -p "$OUTPUT_DIR"
cp "$BUILD_DIR/libaacp_core.so" "$OUTPUT_DIR/"

# Copy c++_shared runtime (cần bundle cùng app)
CPPSHARED="$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"
if [[ -f "$CPPSHARED" ]]; then
    cp "$CPPSHARED" "$OUTPUT_DIR/"
    info "Copied libc++_shared.so"
fi

# ── Verify symbols ────────────────────────────────────────────────────────────
info "Exported symbols:"
"$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm" \
    --defined-only --extern-only --demangle \
    "$OUTPUT_DIR/libaacp_core.so" | grep "^[0-9a-f]" | grep -v "__" | \
    awk '{print "  ✓", $3}'

# ── File info ─────────────────────────────────────────────────────────────────
SO_SIZE=$(du -sh "$OUTPUT_DIR/libaacp_core.so" | cut -f1)
info "Done!"
echo -e "  Output : ${GREEN}$OUTPUT_DIR/libaacp_core.so${NC} ($SO_SIZE)"
echo -e "  Build  : $BUILD_TYPE"
