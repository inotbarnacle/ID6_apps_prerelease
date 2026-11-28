// speedlock_unlock.cpp — Optional standalone in-process NBT EVO HMI plugin
// that disables CRhmiSpeedlockServer (RHMI / custom ID6 app speedlock) once
// at boot via IRhmiSpeedlockService::vtable[4] (setSpeedlockDisabled(true)).
//
// Runs on the HMI UI/dispatcher thread via bmw::Runnable; never blocks boot,
// uses 0% CPU after succeeding, touches no OEM binaries or reg_hu.efs.ffs,
// and survives ConnectedDrive GetPortalAccessData provisioning updates for
// the entire boot session.
#include "plugin_factory.h"
#include <dlfcn.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

namespace {

typedef void* (*GetCacheByIdFn)(const char* identifier);
typedef void (*SetSpeedlockDisabledFn)(void* service, bool disabled);

// GCC 4.4 libstdc++ std::unordered_map<void*, CPluginInstanceCacheTLS*> node
// layout verified in libhmiplugin.so CPluginInstanceCache::getCurrentTLS (ID5_2211).
struct CacheBucketNode {
    void* scopeKey;
    void* tls;
    CacheBucketNode* next;
};

const char kLogPath[] = "/tmp/speedlock_unlock.log";
const unsigned kInitialDelayMs = 1000u;
const unsigned kRetryDelayMs = 2000u;
const unsigned kMaxAttempts = 30u;

void logLine(const char* msg, unsigned attempt, void* ptr) {
    FILE* f = fopen(kLogPath, "a");
    if (!f) return;
    fprintf(f, "pid=%d t=%ld attempt=%u ptr=%p %s\n",
            (int)getpid(), (long)time(0), attempt, ptr, msg);
    fclose(f);
}

bool callSetDisabled(void* svc, unsigned attempt, const char* source) {
    void** svcVtbl = *reinterpret_cast<void***>(svc);
    if (!svcVtbl || !svcVtbl[4]) {
        logLine("IRhmiSpeedlockService vtable[4] is null", attempt, svc);
        return false;
    }
    // IRhmiSpeedlockService vtable[4] (+0x10):
    //   sets CRhmiSpeedlockServer::m_bSpeedlockDisabled = true (+0x96)
    //   and tail-calls CRhmiSpeedlockServer::evaluateTriggerConditions().
    SetSpeedlockDisabledFn setDisabled = reinterpret_cast<SetSpeedlockDisabledFn>(svcVtbl[4]);
    setDisabled(svc, true);
    logLine(source, attempt, svc);
    return true;
}

bool tryDisableRhmiSpeedlock(unsigned attempt) {
    void* symCache = dlsym(RTLD_DEFAULT, "_ZN9hmiplugin28getInstanceCacheByIdentifierEPKc");
    if (!symCache) {
        logLine("dlsym getInstanceCacheByIdentifier failed", attempt, 0);
        return false;
    }
    GetCacheByIdFn getCache = reinterpret_cast<GetCacheByIdFn>(symCache);
    void* cache = getCache("IRhmiSpeedlockService");
    if (!cache) {
        logLine("IRhmiSpeedlockService cache is null", attempt, 0);
        return false;
    }

    // Walk CPluginInstanceCache's unordered_map<scope*, CPluginInstanceCacheTLS*>
    // ([cache + 0x18] = buckets, [cache + 0x1c] = bucket_count) to grab the live
    // IRhmiSpeedlockService* already resolved by HmiMain3D's main thread.
    char* base = static_cast<char*>(cache);
    CacheBucketNode** buckets = *reinterpret_cast<CacheBucketNode***>(base + 0x18);
    unsigned bucketCount = *reinterpret_cast<unsigned*>(base + 0x1c);
    if (!buckets || bucketCount == 0u || bucketCount > 65536u) {
        logLine("CPluginInstanceCache buckets not ready", attempt, cache);
        return false;
    }

    for (unsigned i = 0u; i < bucketCount; ++i) {
        CacheBucketNode* node = buckets[i];
        unsigned hops = 0u;
        while (node && hops < 64u) {
            if (node->tls) {
                void* svc = *reinterpret_cast<void**>(static_cast<char*>(node->tls) + 8);
                if (svc) {
                    return callSetDisabled(
                        svc, attempt,
                        "disabled RHMI speedlock via CPluginInstanceCache IRhmiSpeedlockService::vtable[4](true)");
                }
            }
            node = node->next;
            ++hops;
        }
    }

    logLine("no populated CPluginInstanceCacheTLS entry for IRhmiSpeedlockService yet", attempt,
            reinterpret_cast<void*>(static_cast<unsigned long>(bucketCount)));
    return false;
}

class UnlockRunnable : public bmw::Runnable {
    bmw::PluginApp* app_;
    unsigned attempts_;
public:
    explicit UnlockRunnable(bmw::PluginApp* app) : app_(app), attempts_(0u) {}
    virtual void run() {
        ++attempts_;
        if (tryDisableRhmiSpeedlock(attempts_)) {
            delete this;
            return;
        }
        if (attempts_ < kMaxAttempts) {
            app_->post(this, kRetryDelayMs);
            return;
        }
        logLine("gave up waiting for IRhmiSpeedlockService", attempts_, 0);
        delete this;
    }
};

class SpeedlockUnlockApp : public bmw::PluginApp {
public:
    SpeedlockUnlockApp(bmw::IDispatcher& disp, bmw::BasicString<char> name,
                       unsigned int major, unsigned int minor,
                       bmw::RHMIInterfaceVersion rhmi,
                       const bmw::Bundle& cfg, const bmw::Bundle& res)
        : bmw::PluginApp(disp, name, major, minor, rhmi, cfg, res) {}

    virtual bool onInit() {
        logLine("SpeedlockUnlock::onInit", 0u, this);
        post(new UnlockRunnable(this), kInitialDelayMs);
        return true;
    }
};

}  // namespace

extern "C" {
PluginAppInfo pluginAppInfo = {
    1u,
    "SpeedlockUnlock",
    "owner-development",
    1u,
    create_app<SpeedlockUnlockApp>,
    destroy_app
};
}
