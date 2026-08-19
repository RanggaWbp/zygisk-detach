#ifndef ZYGISK_DETACH_COMMON_H
#define ZYGISK_DETACH_COMMON_H

#include <string>
#include <vector>
#include <set>
#include <map>
#include <mutex>
#include <atomic>
#include <functional>
#include <optional>

#include <jni.h>
#include <android/log.h>
#include <sys/system_properties.h>

// ============================================================
// Version Detection
// ============================================================
#define ANDROID_VERSION_UNKNOWN  0
#define ANDROID_N               24  // Android 7.0
#define ANDROID_N_MR1           25  // Android 7.1
#define ANDROID_O               26  // Android 8.0
#define ANDROID_O_MR1           27  // Android 8.1
#define ANDROID_P               28  // Android 9
#define ANDROID_Q               29  // Android 10
#define ANDROID_R               30  // Android 11
#define ANDROID_S               31  // Android 12
#define ANDROID_S_V2            32  // Android 12L
#define ANDROID_TIRAMISU        33  // Android 13
#define ANDROID_UPSIDE_DOWN     34  // Android 14
#define ANDROID_VANILLA_ICE     35  // Android 15

// ============================================================
// OEM Detection
// ============================================================
enum class OEMType {
    UNKNOWN,
    STOCK_AOSP,
    MIUI,
    HYPEROS,
    ONEUI,          // Samsung
    COLOROS,        // OPPO
    ORIGINOS,       // Vivo
    EMUI,           // Huawei
    FLYME,          // Meizu
    MAGICOS,        // Honor
    REALME_UI,
    OXYGENOS,       // OnePlus (older)
    FUNTOUCHOS      // Vivo
};

// ============================================================
// Hook Layer Priority
// ============================================================
enum class HookLayer {
    LAYER_1_JAVA = 1,       // Java-level ART method hook
    LAYER_2_BINDER = 2,     // Binder proxy hook
    LAYER_3_PROVIDER = 3,   // ContentProvider hook
    LAYER_4_NATIVE = 4,     // Native inline hook
    LAYER_NONE = 0
};

// ============================================================
// Configuration
// ============================================================
struct DetachConfig {
    bool debug = false;
    bool force_detach = true;
    bool hide_from_play = true;
    bool block_update_check = true;
    bool aggressive_mode = false;
    std::vector<std::string> detached_packages;
    
    static DetachConfig load(const std::string& path);
    void save(const std::string& path) const;
};

// ============================================================
// Logger
// ============================================================
class Logger {
private:
    static std::atomic<bool> debug_enabled;
    static std::string log_buffer;
    static std::mutex log_mutex;

public:
    static void setDebug(bool enable) { debug_enabled = enable; }
    static void log(const std::string& tag, const std::string& message);
    static void error(const std::string& tag, const std::string& message);
    static void debug(const std::string& tag, const std::string& message);
    static void flushToFile(const std::string& path);
};

// Convenience macros
#define LOG_TAG "ZygiskDetach"
#define LOGI(...) Logger::log(LOG_TAG, __VA_ARGS__)
#define LOGE(...) Logger::error(LOG_TAG, __VA_ARGS__)
#define LOGD(...) Logger::debug(LOG_TAG, __VA_ARGS__)

// ============================================================
// Android Version Helpers
// ============================================================
class AndroidVersion {
private:
    static int cached_sdk_int;

public:
    static int get();
    static bool isAtLeast(int sdk);
    static bool isModernAndroid(); // >= 12
    static bool hasAIDLPackageManager(); // >= 12
    static std::string getVersionString();
};

// ============================================================
// OEM Detector
// ============================================================
class OEMDetector {
private:
    static OEMType detected_oem;
    static bool detected;

public:
    static OEMType detect();
    static OEMType get();
    static std::string getOEMName();
    static bool isMIUI() { return get() == OEMType::MIUI || get() == OEMType::HYPEROS; }
    static bool isSamsung() { return get() == OEMType::ONEUI; }
    static bool isOPPO() { return get() == OEMType::COLOROS || get() == OEMType::REALME_UI; }
    static bool isHuawei() { return get() == OEMType::EMUI; }
};

// ============================================================
// Package Info structure (internal representation)
// ============================================================
struct PackageInfoInternal {
    std::string package_name;
    std::string installer;
    long version_code;
    std::string version_name;
    bool is_detached;
};

#endif // ZYGISK_DETACH_COMMON_H
