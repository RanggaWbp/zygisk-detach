#!/system/bin/sh
# ============================================
# Service script - runs after boot completed
# ============================================

MODDIR="${0%/*}"
DETACH_DIR="/data/adb/detach"
PLAY_STORE_PKG="com.android.vending"

# Create detach directory if not exists
mkdir -p "$DETACH_DIR"

# Initialize config if not exists
if [ ! -f "$DETACH_DIR/detach.list" ]; then
    touch "$DETACH_DIR/detach.list"
fi

if [ ! -f "$DETACH_DIR/config.json" ]; then
    echo '{"debug": false, "force_detach": true, "hide_from_play": true}' > "$DETACH_DIR/config.json"
fi

# Wait for boot to complete
while [ "$(getprop sys.boot_completed)" != "1" ]; do
    sleep 1
done

# Additional boot delay for stability
sleep 5

# Force stop Play Store to re-inject with our module
am force-stop "$PLAY_STORE_PKG" 2>/dev/null

# Clear Play Store cache (optional, helps on some ROMs)
pm clear "$PLAY_STORE_PKG" 2>/dev/null || true

# Log
echo "$(date '+%Y-%m-%d %H:%M:%S') Detach service started" >> "$DETACH_DIR/detach.log"
