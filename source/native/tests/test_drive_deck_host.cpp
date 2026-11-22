// Runs ShiftDeck or LaunchDeck on the host against the fake framework (tests/fake_bmw.cpp).
// Build with -DDECK_SOURCE='"<plugin .cpp>"' -DDECK_CLASS=ShiftDeck -DDECK_PORT=42863.
//   test-...-deck display   prints every model update, step by step (must not change between revisions)
//   test-...-deck polling   checks the poll timer's lifecycle: stops while hidden, restarts once on show
#include "fake_bmw.h"
#include DECK_SOURCE
#include <map>
#include <set>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static DECK_CLASS* app;
static int sender = -1;

static bmw::IDispatcher& dispatcher() { static char storage[16]; return *reinterpret_cast<bmw::IDispatcher*>(storage); }
static const bmw::Bundle& bundle() { static char storage[16]; return *reinterpret_cast<const bmw::Bundle*>(storage); }

static void start() {
    fake_reset();
    bmw::RHMIInterfaceVersion version = {1};
    delete app;
    app = new DECK_CLASS(dispatcher(), bmw::BasicString<char>("Deck"), 1u, 0u, version, bundle(), bundle());
    app->onInit();
}

static void set_visible(bool visible) { bmw::Parcel event(visible); fake_fire(100u, 11u, event); }

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

static void send_frame_at(int rpm, int kph, uint64_t sampled_ms) {
    drive_frame f;
    memset(&f, 0, sizeof f);
    f.sampled_ms = sampled_ms;
    f.rpm = rpm;
    f.kph = kph;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(DECK_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (sender < 0) sender = socket(AF_INET, SOCK_DGRAM, 0);
    sendto(sender, &f, sizeof f, 0, (struct sockaddr*)&a, sizeof a);
    usleep(5000);
}
static void send_frame(int rpm, int kph) { send_frame_at(rpm, kph, bd_mono_ms()); }

// Lets the plugin's 50 ms RPM ease finish, so what is recorded does not depend on timing.
static void settle() { tick(); usleep(80000); tick(); }

// Prints the models whose final value changed since the last step, in model order. How many updateModel
// calls it took is not part of the transcript (it goes to stderr), so cheaper code does not change it.
static std::map<unsigned, std::string> state, last_state;
static void dump(const char* step) {
    unsigned calls = (unsigned)fake_updates.size();
    for (size_t i = 0; i < fake_updates.size(); ++i) {
        const FakeUpdate& u = fake_updates[i];
        char value[256];
        if (u.kind == 's') snprintf(value, sizeof value, "text '%s'", u.text.c_str());
        else snprintf(value, sizeof value, "%s %u", u.kind == 'i' ? "image" : "number", u.number);
        state[u.model] = value;
    }
    fake_updates.clear();
    unsigned changed = 0;
    std::string lines;
    for (std::map<unsigned, std::string>::const_iterator it = state.begin(); it != state.end(); ++it) {
        std::map<unsigned, std::string>::const_iterator old = last_state.find(it->first);
        if (old != last_state.end() && old->second == it->second) continue;
        char line[320];
        snprintf(line, sizeof line, "  %u %s\n", it->first, it->second.c_str());
        lines += line;
        ++changed;
    }
    last_state = state;
    printf("== %s (%u models changed)\n%s", step, changed, lines.c_str());
    fprintf(stderr, "calls %-60s %u\n", step, calls);
}

static int display() {
    start();
    set_visible(true);
    tick();
    dump("shown, no data yet");
    send_frame(3000, 50); settle(); dump("first frame");
    send_frame(3000, 50); settle(); dump("same frame again");
    send_frame(5200, 90); settle(); dump("rpm and speed change");
    send_frame(7200, 130); settle(); dump("above the shift point");
    usleep(900000); settle(); dump("0.9 s without data (stale)");
    set_visible(false); tick(); dump("hidden");
    set_visible(true); send_frame(2500, 40); settle(); dump("shown again with a frame");
#ifdef DECK_LAUNCH
    // Two timed runs with made-up sample times: a first one (a new best, gold digits), then a slower one.
    static const int fast[] = {0, 0, 20, 50, 80, 100, 110};
    static const int slow[] = {0, 0, 10, 25, 40, 55, 70, 85, 100, 110};
    const int* runs[2] = {fast, slow};
    const unsigned lengths[2] = {sizeof fast / sizeof fast[0], sizeof slow / sizeof slow[0]};
    const unsigned step_ms[2] = {50, 60};
    for (int r = 0; r < 2; ++r) {
        if (r == 0) { set_visible(false); tick(); set_visible(true); tick(); }
        usleep(r == 0 ? 800000 : 600000);
        uint64_t base = bd_mono_ms() - 650;  // every sample must be less than 750 ms old to count as live
        for (unsigned i = 0; i < lengths[r]; ++i) { send_frame_at(0, runs[r][i], base + i * step_ms[r]); tick(); }
        dump(r == 0 ? "first timed run finished" : "second, slower timed run finished");
    }
#endif
    delete app; app = 0;
    return 0;
}

static int failures;
static void check(bool ok, const char* what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static std::set<unsigned> models_updated() {
    std::set<unsigned> m;
    for (size_t i = 0; i < fake_updates.size(); ++i) m.insert(fake_updates[i].model);
    return m;
}
static size_t pending() { size_t n = 0; for (size_t i = 0; i < fake_posts.size(); ++i) n += fake_posts[i].id == 0x40000003u; return n; }

static int polling() {
    start();
    check(pending() == 1, "init starts one poll timer");
    tick();
    check(pending() == 0, "hidden, with no worker: the timer is not re-armed");
    set_visible(true);
    check(pending() >= 1, "showing the page starts the timer");
    for (int i = 0; i < 20; ++i) tick();
    check(pending() == 1, "20 ticks later exactly one timer is running");
    check(fake_posts[0].delay == 16u, "visible poll interval is 16 ms");
    set_visible(false);
    tick(); tick();
    check(pending() == 0, "hiding stops the timer once the worker is gone");
    set_visible(true);
    set_visible(false);
    set_visible(true);
    tick(); tick(); tick();
    check(pending() == 1, "show, hide, show quickly leaves exactly one timer");
    start();
    tick();
    set_visible(true);
    tick();
    send_frame(4000, 60);
    tick();
    check(!fake_updates.empty(), "after a hidden start, data shows once the page is visible");
    // After onInit() again (the HMI re-initialised the plugin) every model must be sent once more.
    start(); set_visible(true); tick();
    std::set<unsigned> fresh = models_updated();
    start(); set_visible(true); tick(); send_frame(4000, 60); settle(); fake_updates.clear();
    app->onInit(); set_visible(true); tick();
    check(models_updated() == fresh, "onInit() again resends every model");
    delete app; app = 0;
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "display")) return display();
    if (argc == 2 && !strcmp(argv[1], "polling")) return polling();
    fprintf(stderr, "usage: %s display|polling\n", argv[0]);
    return 2;
}
