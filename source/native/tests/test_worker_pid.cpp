/* Run each production plugin with an auto-reaping parent and a still-live worker. */
#include "gauge_core.h"
#include "n57.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static uint64_t now=100000;
static unsigned launches,terminations,probes;
static pid_t fixture_pid;
static int output=-1,wait_error,probe_error;
static bool alive;

static inline uint64_t fixture_clock() { return now; }
static pid_t fixture_launch(const char*const*,int channel) {
    assert(!alive); // Losing a live PID must not permit a duplicate launch.
    alive=true;fixture_pid=1000+(++launches);
    assert(output<0);
    if (channel>=0) { output=dup(channel);assert(output>=0); }
    return fixture_pid;
}
static pid_t fixture_waitpid(pid_t pid,int* status,int flags) {
    assert(pid==fixture_pid && flags==WNOHANG);
    if (wait_error && (alive || wait_error==ECHILD)) { errno=wait_error;return -1; }
    if (alive) return 0;
    if (status) *status=0;
    return pid;
}
static int fixture_kill(pid_t pid,int sig) {
    assert(pid==fixture_pid);
    if (!sig) {
        ++probes;
        if (!alive) { errno=ESRCH;return -1; }
        if (probe_error) { errno=probe_error;return -1; }
        return 0;
    }
    assert(sig==SIGTERM);
    ++terminations;
    if (!alive) { errno=ESRCH;return -1; }
    return 0; // Model a worker whose bounded shutdown has not finished yet.
}
static inline unsigned fixture_index(const char*) { return 0x010203; }
static inline int fixture_settings(unsigned* index) { *index=0x010203;return 1; }

#define DIAGNOSTIC_WORKER_H
#define launch fixture_launch
#define waitpid fixture_waitpid
#define kill fixture_kill
#define bd_mono_ms fixture_clock
#define n57_index_file fixture_index
#define n57_settings fixture_settings
#include ID6_PID_SOURCE
#undef launch
#undef waitpid
#undef kill
#undef bd_mono_ms
#undef n57_index_file
#undef n57_settings

static void start(bmw::PluginApp& app) {
#if ID6_PID_ACTION
#if ID6_PID_VISIBLE
    app.testEvent(100,true);
#endif
    app.testAction(ID6_PID_ACTION);
#else
    app.testEvent(100,true);
#endif
}

static void poll(bmw::PluginApp& app) {
    assert(!app.messages.empty());
    ++now;
    app.testDeliver(static_cast<unsigned>(app.messages.size()-1));
}

static void stop(bmw::PluginApp& app) {
#if ID6_PID_HUD
    app.testAction(21); // HUD remains enabled while its menu is hidden.
#else
    app.onFinalize();
#endif
}

static void exited() {
    alive=false;
    if (output>=0) { close(output);output=-1; }
}

static void check_live(int error,int probe) {
    wait_error=error;probe_error=probe;
    const unsigned before=launches;
    bmw::IDispatcher dispatcher;bmw::Bundle bundle;
    bmw::PluginApp* app=pluginAppInfo.create(dispatcher,pluginAppInfo.name,bundle,bundle);
    assert(app->onInit());start(*app);
    if (!alive) poll(*app);
    assert(alive && launches==before+1);
    for (unsigned i=0;i<8;++i) { now+=2100;poll(*app); }
    assert(launches==before+1);
    const unsigned signals=terminations;
    stop(*app);
    assert(terminations>signals); // MENU/finalization must still own and stop that PID.
    exited();
#if ID6_PID_HUD
    poll(*app);
#endif
    pluginAppInfo.destroy(app);
}

static void check_dead(int error) {
    wait_error=error;probe_error=0;
    const unsigned before=launches;
    bmw::IDispatcher dispatcher;bmw::Bundle bundle;
    bmw::PluginApp* app=pluginAppInfo.create(dispatcher,pluginAppInfo.name,bundle,bundle);
    assert(app->onInit());start(*app);
    if (!alive) poll(*app);
    assert(launches==before+1);
    exited();
    const unsigned signals=terminations;
#if ID6_PID_HUD
    poll(*app); // Observe exit before unchecking; no worker needs a stop signal.
#endif
    stop(*app);pluginAppInfo.destroy(app);
    assert(terminations==signals);
}

int main() {
    unsigned index=0;
    assert(fixture_clock()==now && fixture_index(0)==0x010203);
    assert(fixture_settings(&index)==1 && index==0x010203);
    check_live(ECHILD,0);
    check_live(ECHILD,EPERM);
    check_live(ECHILD,EIO);
    check_live(EINTR,0);
    check_live(0,0);
    check_dead(ECHILD);
    check_dead(0);
    assert(probes>0);
    printf("%s: live ECHILD/EPERM/error/EINTR retained, duplicate launch blocked, stop and confirmed exit OK\n",pluginAppInfo.name);
}
