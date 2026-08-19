#!/system/bin/sh
# ============================================
# Post-fs-data - runs early in boot
# ============================================

MODDIR="${0%/*}"
DETACH_DIR="/data/adb/detach"

# Create necessary directories
mkdir -p "$DETACH_DIR"

# Handle SELinux context for MIUI/China ROMs
if [ -f "/sys/fs/selinux/enforce" ]; then
    CURRENT_SELINUX=$(cat /sys/fs/selinux/enforce)
    if [ "$CURRENT_SELINUX" = "1" ]; then
        # We don't disable SELinux, but set permissive for our operations
        # This is safer than disabling entirely
        chcon u:object_r:magisk_file:s0 "$DETACH_DIR" 2>/dev/null
    fi
fi

# For MIUI: Disable MIUI optimization for Play Store
# (This helps with package visibility issues)
MIUI_VERSION=$(getprop ro.miui.ui.version.name)
if [ "$MIUI_VERSION" != "" ]; then
    # Set MIUI-specific props
    setprop persist.sys.miui_optimization false 2>/dev/null
    resetprop persist.sys.miui_optimization false 2>/dev/null
fi

# Wait for Zygisk to be ready
while [ ! -e "/data/adb/modules/zygisk_detach_v2" ]; do
    sleep 0.5
done

exit 0
