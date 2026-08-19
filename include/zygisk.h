// Zygisk API v4 header - compatible with Magisk, KernelSU, APatch
// This is a simplified version for the module

#ifndef ZYGISK_H
#define ZYGISK_H

#include <jni.h>

#define ZYGISK_API_VERSION 4

namespace zygisk {

struct Api;
struct AppSpecializeArgs;
struct ServerSpecializeArgs;

enum class StateFlag : uint32_t {
    PROCESS_GRANTED_ROOT = (1u << 0),
    PROCESS_ON_DENYLIST = (1u << 1),
};

enum class Option : int {
    FORCE_DENYLIST_UNMOUNT = 0,
    DLCLOSE_MODULE_LIBRARY = 1,
};

struct AppSpecializeArgs {
    jint &uid;
    jint &gid;
    jintArray &gids;
    jint &runtime_flags;
    jintArray &rlimits;
    jint &mount_external;
    jstring &se_info;
    jstring &nice_name;
    jstring &instruction_set;
    jstring &app_data_dir;

    /* Optional */
    jstring *package_name;
    jstring *app_dir;
    jstring *split_se_info;
    jboolean *is_top_app;
    jlongArray *pkg_data_info_list;
    jlongArray *isolated_mount_namespace;
    jboolean *has_forked;
    jboolean *is_child_zygote;
    jboolean *usap_table_enabled;

    AppSpecializeArgs() = delete;
};

struct ServerSpecializeArgs {
    jint &uid;
    jint &gid;
    jintArray &gids;
    jint &runtime_flags;
    jlongArray &rlimits;
    jint &mount_external;
    jstring &se_info;
    jstring &nice_name;
    jboolean &start_child_zygote;
    jboolean &usap_table_enabled;

    ServerSpecializeArgs() = delete;
};

using AppSpecializeType = void (*)(const AppSpecializeArgs *args);
using ServerSpecializeType = void (*)(const ServerSpecializeArgs *args);
using PreAppSpecializeType = void (*)(const AppSpecializeArgs *args);
using PostAppSpecializeType = void (*)(const AppSpecializeArgs *args);
using PreServerSpecializeType = void (*)(const ServerSpecializeArgs *args);
using PostServerSpecializeType = void (*)(const ServerSpecializeArgs *args);

struct ApiTable {
    void *impl;
    bool (*registerModule)(ApiTable *table, long *module);
    void (*hookJniNativeMethods)(const char *className,
                                  const char *method, void *fn, void **backup);
    void (*pltHookRegister)(const char *regex, const char *symbol, void *fn, void **backup);
    void (*pltHookExclude)(const char *regex);
    bool (*pltHookCommit)();
    void (*connectCompanion)(int fd);
    int (*getModuleDir)();
    int (*getFlags)(StateFlag *);
    void (*setOption)(Option opt);
};

struct ModuleBase {
    virtual void onLoad(Api *api, JNIEnv *env) {}
    virtual void preAppSpecialize(AppSpecializeArgs *args) {}
    virtual void postAppSpecialize(const AppSpecializeArgs *args) {}
    virtual void preServerSpecialize(ServerSpecializeArgs *args) {}
    virtual void postServerSpecialize(const ServerSpecializeArgs *args) {}
};

struct Api {
    ApiTable *table;

    void setOption(Option opt) {
        table->setOption(opt);
    }

    int connectCompanion(int fd) {
        table->connectCompanion(fd);
        return 0;
    }

    int getModuleDir() {
        return table->getModuleDir();
    }

    void hookJniNativeMethods(const char *className,
                              const char *method,
                              void *fn,
                              void **backup) {
        table->hookJniNativeMethods(className, method, fn, backup);
    }

    void pltHookRegister(const char *regex, const char *symbol,
                        void *fn, void **backup) {
        table->pltHookRegister(regex, symbol, fn, backup);
    }

    void pltHookExclude(const char *regex) {
        table->pltHookExclude(regex);
    }

    bool pltHookCommit() {
        return table->pltHookCommit();
    }
};

} // namespace zygisk

#define REGISTER_ZYGISK_MODULE(module_class)                    \
    void zygisk_module_entry(zygisk::ApiTable *table, JNIEnv *env) { \
        static zygisk::Api api = {table};                       \
        static module_class module;                             \
        module.onLoad(&api, env);                               \
        zygisk_module = &module;                                \
    }

extern zygisk::ModuleBase *zygisk_module;

extern "C" [[gnu::visibility("default")]] \
void zygisk_companion_entry(int fd);

#endif // ZYGISK_H
