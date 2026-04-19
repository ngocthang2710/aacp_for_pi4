#!/usr/bin/env bash
# =============================================================================
# build_aar.sh — Build aacp-sdk-1.0.0.aar via Gradle
#
# Output: outputs/aacp-sdk-1.0.0.aar
#
# .aar chứa:
#   - classes.jar  (Kotlin bytecode: CarPlayManager, VideoDecoder, etc.)
#   - jni/arm64-v8a/libaacp_jni.so  (JNI bridge)
#   - jni/arm64-v8a/libaacp_core.so (C++ core)
#   - jni/arm64-v8a/libc++_shared.so (STL runtime)
#   - AndroidManifest.xml
#   - proguard.txt
#
# Consumer dùng .aar bằng cách:
#   1. Copy vào app/libs/
#   2. Trong build.gradle.kts: implementation(files("libs/aacp-sdk-1.0.0.aar"))
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[build_aar]${NC} $*"; }
warn()  { echo -e "${YELLOW}[build_aar]${NC} $*"; }
error() { echo -e "${RED}[build_aar] ERROR:${NC} $*" >&2; exit 1; }

BUILD_TYPE="${1:-release}"  # release | debug

# ── Validate environment ──────────────────────────────────────────────────────
if ! command -v java &>/dev/null; then
    error "Java not found. Install JDK 17."
fi
JAVA_VER=$(java -version 2>&1 | head -1 | grep -o '[0-9]*\.[0-9]*' | head -1)
info "Java version: $JAVA_VER"

if [[ ! -f "$PROJECT_ROOT/gradlew" ]]; then
    warn "gradlew not found — generating wrapper..."
    cd "$PROJECT_ROOT"
    gradle wrapper --gradle-version=8.7 2>/dev/null || \
        error "Cannot generate gradle wrapper. Install Gradle or run: gradle wrapper"
fi

# ── Build ─────────────────────────────────────────────────────────────────────
cd "$PROJECT_ROOT"
info "Building AAR (${BUILD_TYPE})..."

if [[ "$BUILD_TYPE" == "release" ]]; then
    ./gradlew :sdk-android:assembleRelease :sdk-android:copyAarToOutputs \
        --no-daemon --stacktrace 2>&1 | tail -20
else
    ./gradlew :sdk-android:assembleDebug \
        --no-daemon --stacktrace 2>&1 | tail -20
    # Copy debug AAR
    mkdir -p "$PROJECT_ROOT/outputs"
    cp "$PROJECT_ROOT/sdk-android/build/outputs/aar/sdk-android-debug.aar" \
       "$PROJECT_ROOT/outputs/aacp-sdk-1.0.0-debug.aar"
fi

# ── Inspect .aar contents ─────────────────────────────────────────────────────
AAR_FILE="$PROJECT_ROOT/outputs/aacp-sdk-1.0.0.aar"
if [[ -f "$AAR_FILE" ]]; then
    AAR_SIZE=$(du -sh "$AAR_FILE" | cut -f1)
    info "AAR contents:"
    unzip -l "$AAR_FILE" | awk 'NR>2 && NF>3 {
        size=$1; name=$4
        if (size > 0) printf "  %-50s %s bytes\n", name, size
    }' | head -20

    echo ""
    info "Done!"
    echo -e "  Output : ${GREEN}$AAR_FILE${NC} ($AAR_SIZE)"
else
    error "AAR not found at expected path: $AAR_FILE"
fi
