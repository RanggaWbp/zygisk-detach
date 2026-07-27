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

// Berapa banyak word (uint32_t) yang kita coba scan sebelum menyerah.
// Header binder bisa berbeda panjang antar SDK/ROM (strict-mode policy,
// work-source uid, dll) - daripada hardcode 1 offset seperti versi lama
// (getBinderHeadersLen(sdk) yang cuma lihat SDK level), kita coba
// beberapa kandidat offset dan VALIDASI ISINYA. Ini fix utk root cause
// "detach selalu gagal" di ROM yang layout parcel-nya beda dari AOSP.
#define MAX_HEADER_SCAN_WORDS 8

static char* DETACH_TXT = nullptr;
static size_t DETACH_TXT_SIZE = 0;
static uint32_t getApplicationEnabledSetting_code = 0;

// Cek apakah pada offset `header_words` word dari awal parcel terdapat
// string16 yang cocok dengan PM_DESC. Kalau cocok, kembalikan posisi
// byte tepat SETELAH descriptor lewat out_cursor.
static inline bool try_match_desc_at(FakeParcel parcel, uint32_t header_words,
                                      size_t data_size, uint32_t* out_cursor) {
    parcel.skip(header_words * sizeof(uint32_t));

    if ((size_t)parcel.getCursor() + 4 > data_size) return false;

    uint32_t descLen = parcel.readInt32();
    // sanity bound: nama kelas Java gak mungkin sepanjang itu, dan ini
    // mencegah readString16 lompat keluar buffer kalau offset salah
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

    // Coba tiap kemungkinan panjang header alih-alih percaya satu nilai
    // tetap berdasarkan SDK level saja - inilah bug lama yang bikin
    // detach gagal total di sebagian device/ROM.
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
    body.skip(2);  // trailing policy byte after descriptor (unchanged from upstream)

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
        if (i > DETACH_TXT_SIZE) break;  // detach.bin corrupt - stop, jangan OOB read
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

    if (!api->pltHookRegister(dev, inode, "_ZN7android14IPCThreadState8transactEijRKNS_6ParcelEPS1_j",
                              (void**)&transact_hook, (void**)&transact_orig)) {
        LOGD("ERROR: pltHookRegister failed");
        return false;
    }
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
