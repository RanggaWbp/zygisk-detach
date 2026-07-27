#include "module.hpp"

#include <android/log.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include "binder.hpp"
#include "zygisk.hpp"

#define ARR_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define STR_LEN(a) (ARR_LEN(a) - 1)
#define VENDING_PROC "com.android.vending"

#define PM_DESC u"android.content.pm.IPackageManager"

// How many words (uint32_t) we try to scan before giving up.
// The binder header length can vary between SDK versions and ROMs
// (strict-mode policy, work-source uid, etc.) - rather than hardcoding a
// single offset like the old approach (getBinderHeadersLen(sdk), which only
// looked at the SDK level), we try several candidate offsets and VALIDATE
// their content. This fixes the root cause of "detach always fails" on ROMs
// whose parcel layout diverges from AOSP.
#define MAX_HEADER_SCAN_WORDS 8

static char* DETACH_TXT = nullptr;
static size_t DETACH_TXT_SIZE = 0;
static uint32_t getApplicationEnabledSetting_code = 0;

// Check whether, at an offset of `header_words` words from the start of the
// parcel, there is a string16 matching PM_DESC. If it matches, return the
// byte position immediately AFTER the descriptor via out_cursor.
static inline bool try_match_desc_at(FakeParcel parcel, uint32_t header_words,
                                      size_t data_size, uint32_t* out_cursor) {
    parcel.skip(header_words * sizeof(uint32_t));

    if ((size_t)parcel.getCursor() + 4 > data_size) return false;

    uint32_t descLen = parcel.readInt32();
    // Sanity bound: a Java class name is never this long, and this also
    // prevents readString16 from reading past the buffer if the offset
    // guess is wrong.
    if (descLen == 0 || descLen > 512) return false;
    if ((size_t)parcel.getCursor() + (descLen + 1) * sizeof(char16_t) > data_size) return false;

    char16_t* desc = parcel.readString16(descLen);
    if (descLen != STR_LEN(PM_DESC)) return false;
    if (memcmp(desc, PM_DESC, descLen * sizeof(char16_t)) != 0) return false;

    *out_cursor = parcel.getCursor();
    return true;
}

static inline void detach(PParcel* pparcel, uint32_t code) {
    if (code != getApplicationEnabledSetting_code) return;
    if (pparcel->data == nullptr || pparcel->data_size < 8) return;

    auto parcel = FakeParcel(pparcel->data);
    uint32_t cursor = 0;
    bool found = false;

    // Try every plausible header length instead of trusting a single fixed
    // value derived from the SDK level alone - this was the long-standing
    // bug that made detach fail completely on some devices/ROMs.
    for (uint32_t words = 0; words <= MAX_HEADER_SCAN_WORDS; words++) {
        if (try_match_desc_at(parcel, words, pparcel->data_size, &cursor)) {
            found = true;
            LOGD("descriptor matched at header offset = %u word(s)", words);
            break;
        }
    }

    if (!found) {
        LOGD("getApplicationEnabledSetting transact seen but descriptor not found "
             "in first %d words - parcel layout differs on this ROM/SDK",
             MAX_HEADER_SCAN_WORDS);
        return;
    }

    auto body = FakeParcel(pparcel->data);
    body.skip(cursor);
    body.skip(2);  // 2 trailing policy bytes after the descriptor (unchanged from upstream)

    if ((size_t)body.getCursor() + 4 > pparcel->data_size) return;
    auto pkgLen = body.readInt32();
    if (pkgLen == 0 || pkgLen > 255 ||
        (size_t)body.getCursor() + (pkgLen + 1) * sizeof(char16_t) > pparcel->data_size) {
        return;
    }
    auto pkg = body.readString16(pkgLen);

    auto pkgLenB = (uint8_t)(pkgLen * 2 - 1);
    size_t i = 0;
    uint8_t dlen;
    while (i < DETACH_TXT_SIZE && (dlen = DETACH_TXT[i])) {
        const char* dptr = DETACH_TXT + i + sizeof(dlen);
        i += sizeof(dlen) + dlen;
        if (i > DETACH_TXT_SIZE) break;  // detach.bin is corrupt - stop, avoid an out-of-bounds read
        if (dlen != pkgLenB) continue;
        if (memcmp(dptr, pkg, dlen) == 0) {
            *pkg = 0;
            LOGD("detached package (matched %u bytes)", dlen);
            return;
        }
    }
}

int (*transact_orig)(void*, int32_t, uint32_t, void*, void*, uint32_t);

int transact_hook(void* self, int32_t handle, uint32_t code, void* pdata, void* preply, uint32_t flags) {
    auto parcel = (PParcel*)pdata;
    detach(parcel, code);
    return transact_orig(self, handle, code, pdata, preply, flags);
}

static size_t read_companion(int fd) {
    off_t size;
    if (read(fd, &size, sizeof(size)) < 0) {
        LOGD("ERROR read: %s", strerror(errno));
        return 0;
    }
    if (size <= 0) {
        LOGD("ERROR read_companion: size=%ld", size);
        return 0;
    }
    DETACH_TXT = (char*)malloc(size + 1);
    if (!DETACH_TXT) {
        LOGD("ERROR malloc failed for size=%ld", size);
        return 0;
    }

    if (!readFullFromFd(fd, DETACH_TXT, size)) {
        free(DETACH_TXT);
        DETACH_TXT = nullptr;
        return 0;
    }

    DETACH_TXT[size] = 0;
    return (size_t)size;
}

static bool runPreSpecialize(const char* process, zygisk::Api* api) {
    if (memcmp(process, VENDING_PROC, STR_LEN(VENDING_PROC)) != 0) return false;

    int fd = api->connectCompanion();
    if (fd < 0) {
        LOGD("ERROR: connectCompanion failed");
        return false;
    }
    size_t detach_len = read_companion(fd);
    close(fd);
    if (detach_len == 0) return false;
    DETACH_TXT_SIZE = detach_len;

    return true;
}

static bool runPostSpecialize(const char* process, zygisk::Api* api, JNIEnv* env) {
    int sdk = android_get_device_api_level();
    if (sdk <= 0) {
        LOGD("ERROR android_get_device_api_level: %d", sdk);
        return false;
    }

    getApplicationEnabledSetting_code = getStaticIntFieldJni(env, STUB("android/content/pm/IPackageManager"),
                                                             TRSCTN("getApplicationEnabledSetting"));
    if (getApplicationEnabledSetting_code == 0) {
        LOGD("ERROR: could not resolve getApplicationEnabledSetting transaction code "
             "- IPackageManager$Stub layout may have changed on this Android build");
        return false;
    }

    ino_t inode;
    dev_t dev;
    if (!getMapping("libbinder.so", &inode, &dev)) {
        LOGD("ERROR: Could not get libbinder");
        return false;
    }

    api->pltHookRegister(dev, inode, "_ZN7android14IPCThreadState8transactEijRKNS_6ParcelEPS1_j",
                         (void**)&transact_hook, (void**)&transact_orig);
    if (!api->pltHookCommit()) {
        LOGD("ERROR: pltHookCommit");
        return false;
    }

    LOGD("Loaded on %s (sdk=%d, transact_code=%u)", process, sdk, getApplicationEnabledSetting_code);
    return true;
}

class ZygiskDetach : public zygisk::ModuleBase {
   public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        process = env->GetStringUTFChars(args->nice_name, nullptr);
        doRunPost = runPreSpecialize(process, api);

        if (!doRunPost) {
            cleanup(args);
        }
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs* args) override {
        if (!doRunPost) return;

        if (!runPostSpecialize(process, api, env)) {
            cleanup(args);
        }
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs* args) override {
        (void)args;
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

    void cleanup(const zygisk::AppSpecializeArgs* args) {
        free(DETACH_TXT);
        DETACH_TXT = nullptr;
        env->ReleaseStringUTFChars(args->nice_name, process);
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

   private:
    zygisk::Api* api;
    JNIEnv* env;

    bool doRunPost;
    const char* process;
};

static void companionHandler(int remote_fd) {
    companionSendFile("/data/adb/zygisk-detach/detach.bin", remote_fd);
}

REGISTER_ZYGISK_MODULE(ZygiskDetach)
REGISTER_ZYGISK_COMPANION(companionHandler)
