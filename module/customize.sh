#!/system/bin/sh
# ============================================
# Zygisk Detach Fork - Installation Script
# ============================================

SKIPUNZIP=0

# API check
API=$(getprop ro.build.version.sdk)
if [ "$API" -lt 24 ]; then
    abort "! Minimal Android 7.0 (API 24) required"
fi

# Detect architecture
ARCH=$(getprop ro.product.cpu.abi)
ui_print "- Architecture: $ARCH"
ui_print "- Android API: $API"
ui_print "- Device: $(getprop ro.product.model)"

# Detect OEM
OEM=""
if [ -d "/data/miui" ] || [ "$(getprop ro.miui.ui.version.name)" != "" ]; then
    OEM="MIUI"
elif [ "$(getprop ro.build.version.oneui)" != "" ]; then
    OEM="ONEUI"
elif [ "$(getprop ro.build.version.opporom)" != "" ]; then
    OEM="COLOROS"
elif [ "$(getprop ro.build.version.emui)" != "" ]; then
    OEM="EMUI"
fi

if [ "$OEM" != "" ]; then
    ui_print "- Detected OEM: $OEM (Loading compat layer)"
fi

# Extract files
ui_print "- Extracting module files..."
unzip -o "$ZIPFILE" -d "$MODPATH" >/dev/null 2>&1

# Set permissions
ui_print "- Setting permissions..."
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/post-fs-data.sh" 0 0 0755

# Create config directory
mkdir -p "$MODPATH/config"
touch "$MODPATH/config/detach.list"

ui_print " "
ui_print "  ╔══════════════════════════════════════╗"
ui_print "  ║   Zygisk Detach Fork - Installed!     ║"
ui_print "  ║                                      ║"
ui_print "  ║   Add packages to detach in:        ║"
ui_print "  ║   /data/adb/detach/detach.list      ║"
ui_print "  ║                                      ║"
ui_print "  ║   Format: one package per line      ║"
ui_print "  ║   Example: com.example.app          ║"
ui_print "  ╚══════════════════════════════════════╝"
ui_print " "
