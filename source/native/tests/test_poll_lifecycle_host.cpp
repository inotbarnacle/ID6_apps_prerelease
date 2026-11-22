// Runs any plugin that polls on a self-reposting timer (WarmDeck, HybridDeck, ...) on the host against the fake
// framework. Build with -DPLUGIN_SOURCE='"<plugin .cpp>"' -DPLUGIN_CLASS=WarmDeck.
//   display   prints every model update through show / hide / show (must not change between revisions)
//   polling   the poll timer must stop while the page is hidden and the worker is gone, and restart once on show
#include "fake_bmw.h"
#include PLUGIN_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static PLUGIN_CLASS* app;

static void start() {
    static char storage[16];
    fake_reset();
    bmw::RHMIInterfaceVersion version = {1};
    delete app;
    bmw::IDispatcher& d = *reinterpret_cast<bmw::IDispatcher*>(storage);
    const bmw::Bundle& b = *reinterpret_cast<const bmw::Bundle*>(storage);
    app = new PLUGIN_CLASS(d, bmw::BasicString<char>("Deck"), 1u, 0u, version, b, b);
    app->onInit();
}
static void set_visible(bool v) { bmw::Parcel e(v); fake_fire(100u, 11u, e); }
static void tick() {
    std::vector<FakePost> due;
    due.swap(fake_posts);
    for (size_t i = 0; i < due.size(); ++i) {
        if (due[i].id != 0x40000003u || due[i].words.empty()) continue;
        bmw::Parcel data;
        for (size_t j = 1; j < due[i].words.size(); ++j) data.writeUInt32(due[i].words[j]);
        app->invokeDataUpdate(due[i].words[0], data);
    }
}
static size_t pending() { size_t n = 0; for (size_t i = 0; i < fake_posts.size(); ++i) n += fake_posts[i].id == 0x40000003u; return n; }
// Ticks until the timer stops (a worker that was running takes a moment to exit); false if it never does.
static bool stops() {
    for (int i = 0; i < 100 && pending(); ++i) { usleep(10000); tick(); }
    return pending() == 0;
}
static void dump(const char* step) {
    printf("== %s (%u updates)\n", step, (unsigned)fake_updates.size());
    for (size_t i = 0; i < fake_updates.size(); ++i) {
        const FakeUpdate& u = fake_updates[i];
        if (u.kind == 's') printf("  %u text '%s'\n", u.model, u.text.c_str());
        else printf("  %u %s %u\n", u.model, u.kind == 'i' ? "image" : "number", u.number);
    }
    fake_updates.clear();
}

static int display() {
    start(); dump("init");
    set_visible(true); tick(); dump("shown");
    tick(); dump("next tick");
    set_visible(false); stops(); dump("hidden");
    set_visible(true); tick(); dump("shown again");
    delete app; app = 0;
    return 0;
}

static int failures;
static void check(bool ok, const char* what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++failures; }

static int polling() {
    start();
    check(pending() <= 1, "init starts at most one poll timer");
    check(stops(), "hidden, with no worker: the timer stops");
    set_visible(true);
    check(pending() >= 1, "showing the page starts the timer");
    for (int i = 0; i < 20; ++i) tick();
    check(pending() == 1, "20 ticks later exactly one timer is running");
    set_visible(false);
    check(stops(), "hiding stops the timer once the worker is gone");
    set_visible(true); set_visible(false); set_visible(true);
    tick(); tick(); tick();
    check(pending() == 1, "show, hide, show quickly leaves exactly one timer");
    set_visible(false);
    check(stops(), "hiding again stops it");
    delete app; app = 0;
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "display")) return display();
    if (argc == 2 && !strcmp(argv[1], "polling")) return polling();
    fprintf(stderr, "usage: %s display|polling\n", argv[0]);
    return 2;
}
