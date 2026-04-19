#!/usr/bin/env bash
# =============================================================================
# watch_usb.sh — Monitor USB events real-time khi cắm/rút iPhone
#
# Hiển thị:
#   - lsusb changes khi device kết nối/ngắt
#   - usbmon raw packets (nếu Wireshark không có)
#   - AOA mode transition
#
# Usage: sudo ./watch_usb.sh
# =============================================================================
set -uo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; NC='\033[0m'; BOLD='\033[1m'

echo -e "${BOLD}${CYAN}AACP USB Monitor${NC}"
echo "Watching for Apple USB devices... (Ctrl+C to stop)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Snapshot hiện tại
prev_devices=$(lsusb | grep -i "Apple\|05ac" || echo "none")

monitor_lsusb() {
    while true; do
        sleep 0.5
        current=$(lsusb | grep -i "Apple\|05ac" || echo "none")
        if [[ "$current" != "$prev_devices" ]]; then
            timestamp=$(date '+%H:%M:%S.%3N')

            # Devices mới xuất hiện
            while IFS= read -r line; do
                if [[ -n "$line" ]] && ! echo "$prev_devices" | grep -qF "$line"; then
                    PID=$(echo "$line" | grep -o '05ac:[0-9a-f]*' | cut -d: -f2)
                    case "$PID" in
                        2d01) echo -e "[$timestamp] ${GREEN}+ AOA 2.0 mode (with audio)${NC}: $line" ;;
                        2d00) echo -e "[$timestamp] ${GREEN}+ AOA 1.0 mode${NC}: $line" ;;
                        *)    echo -e "[$timestamp] ${YELLOW}+ Apple device${NC}: $line" ;;
                    esac
                fi
            done <<< "$current"

            # Devices biến mất
            while IFS= read -r line; do
                if [[ -n "$line" && "$line" != "none" ]] && ! echo "$current" | grep -qF "$line"; then
                    echo -e "[$timestamp] ${RED}- Disconnected${NC}: $line"
                fi
            done <<< "$prev_devices"

            prev_devices="$current"
        fi
    done
}

# Load usbmon nếu cần
if ! lsmod 2>/dev/null | grep -q usbmon; then
    echo -e "${YELLOW}Loading usbmon for detailed capture...${NC}"
    sudo modprobe usbmon 2>/dev/null && echo "usbmon loaded" || \
        echo -e "${YELLOW}Warning: usbmon not available (non-fatal)${NC}"
fi

# Wireshark tip
if command -v tshark &>/dev/null; then
    echo ""
    echo "Wireshark capture command:"
    echo -e "  ${CYAN}sudo tshark -i usbmon1 -f 'usb.idVendor == 0x05ac' -w /tmp/carplay.pcap${NC}"
fi

echo ""
monitor_lsusb
