// Runs the real BoostDeck plugin on the host against the fake framework (tests/fake_bmw.cpp).
// The plugin source is #included, so BOOST_DECK_SOURCE can point at any revision of boost_deck.cpp.
//   test-boost-deck display   prints every model update, step by step. It must be identical for any
//                             revision that only changes how often or how cheaply the display is built.
//   test-boost-deck polling   checks the poll timer's lifecycle: it must stop while hidden, restart
//                             once on show, and never run twice.
#include "fake_bmw.h"
#include "gauge_preferences.h"
// The plugin's settings path is the car's flash: pretend every save works and write nothing.
static bool fake_save(const char*, const unsigned char*, size_t) { return true; }
#define savePersistentBytes fake_save
#include BOOST_DECK_SOURCE
#include <map>
#include <set>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static BoostDeck* app;
static int sender = -1;
static unsigned sequence;

static bmw::IDispatcher& dispatcher() { static char storage[16]; return *reinterpret_cast<bmw::IDispatcher*>(storage); }
static const bmw::Bundle& bundle() { static char storage[16]; return *reinterpret_cast<const bmw::Bundle*>(storage); }

static void start() {
    fake_reset();
    sequence = 0;
    bmw::RHMIInterfaceVersion version = {1};
    delete app;
    app = new BoostDeck(dispatcher(), bmw::BasicString<char>("BoostDeck"), 1u, 0u, version, bundle(), bundle());
    app->onInit();
}

static void set_visible(bool visible) { bmw::Parcel event(visible); fake_fire(100u, 11u, event); }

// Delivers every pending timer message once, like the HMI's dispatcher would.
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

static bd_frame frame(double boost, long age_shift_ms) {
    bd_frame f;
    memset(&f, 0, sizeof f);
    f.seq = ++sequence;
    f.valid = 0x1ffu;  // the nine default gauges: boost, AFR, timing, IAT, trans, rail, oil, coolant, ethanol
    const double v[9] = {boost, 12.5, 8, 30, 80, 120, 3.5, 90, 40};
    for (unsigned i = 0; i < 9; ++i) f.value[i] = v[i];
    f.sent_ms = bd_mono_ms() - age_shift_ms;
    return f;
}

static void send_packet(const char* packet, size_t n);
static void send_frame(const bd_frame& f) {
    char packet[BD_MAX_PACKET];
    int n = bd_encode(&f, packet, sizeof packet);
    send_packet(packet, (size_t)n);
}

static void send_packet(const char* packet, size_t n) {
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(BD_UDP_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (sender < 0) sender = socket(AF_INET, SOCK_DGRAM, 0);
    sendto(sender, packet, n, 0, (struct sockaddr*)&a, sizeof a);
    usleep(5000);
}

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
    send_frame(frame(10.5, 0)); tick(); dump("first frame");
    send_frame(frame(10.5, 0)); tick(); dump("same values again");
    send_frame(frame(12.0, 0)); tick(); dump("boost changes");
    send_frame(frame(12.04, 0)); tick(); dump("boost changes below the shown precision");
    send_frame(frame(12.0, 5000)); tick(); dump("frame older than the last one is ignored");
    send_frame(frame(15.0, 0)); send_frame(frame(16.0, 0)); send_frame(frame(17.0, 0)); tick(); dump("three frames queued, the newest wins");
    // Several packets in one tick, in an order the network would not normally produce.
    send_frame(frame(20.0, 0)); send_frame(frame(21.0, 300)); tick(); dump("burst: newer packet first, older after it (20.0 stays)");
    { bd_frame a = frame(22.0, 0), b = a; b.value[0] = 23.0; send_frame(a); send_frame(b); tick(); dump("burst: equal sent_ms (the later one, 23.0, wins)"); }
    send_frame(frame(24.0, 0)); send_packet("NBG7 1 0 511 99999999999999 garbage\n", 36); tick(); dump("burst: valid packet, then a broken one claiming a later time (24.0 stays)");
    send_frame(frame(25.0, 0)); send_frame(frame(26.0, -100000)); tick(); dump("burst: a packet from the future is ignored (25.0 stays)");
    // Setting rows fire 2400 + 20 * row + choice (BOOST_PRESSURE 0, SPEED 1, TEMPERATURE 2, STYLE 3).
    { bmw::Parcel e; fake_fire(0u, 2401u, e); } tick(); dump("pressure to bar");
    { bmw::Parcel e; fake_fire(0u, 2441u, e); } tick(); dump("temperature to celsius");
    { bmw::Parcel e; fake_fire(0u, 2461u, e); } tick(); dump("gauge style to circular dials");
    { bmw::Parcel e; fake_fire(0u, 43u, e); } tick(); dump("colour 3");
    set_visible(false); tick(); set_visible(true); tick(); dump("hidden and shown again to reset the sample age");
    send_frame(frame(14.0, 2300)); tick(); dump("frame 2.3 s old (still fresh)");
    usleep(400000); tick(); dump("0.4 s later: the readings with a 2.5 s limit go stale");
    set_visible(false); tick(); dump("hidden");
    set_visible(true); send_frame(frame(9.0, 0)); tick(); dump("shown again with a frame");
    { bmw::Parcel e; fake_fire(0u, 2400u, e); fake_fire(0u, 2440u, e); fake_fire(0u, 2460u, e); } tick(); dump("back to psi, fahrenheit, bars");
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
    check(fake_posts[0].delay == 250u, "idle poll interval is 250 ms");
    tick();
    check(pending() == 0, "hidden, with no collector: the timer is not re-armed");
    set_visible(true);
    check(pending() >= 1, "showing the page starts the timer");
    for (int i = 0; i < 20; ++i) tick();
    check(pending() == 1, "20 ticks later exactly one timer is running");
    set_visible(false);
    tick(); tick();
    check(pending() == 0, "hiding stops the timer once the collector is gone");
    set_visible(true);
    set_visible(false);
    set_visible(true);
    tick(); tick(); tick();
    check(pending() == 1, "show, hide, show quickly leaves exactly one timer");
    start();
    tick();
    set_visible(true);
    tick();
    send_frame(frame(10.5, 0));
    tick();
    check(!fake_updates.empty(), "after a hidden start, data shows once the page is visible");
    // After onInit() again (the HMI re-initialised the plugin) every model must be sent once more.
    start(); set_visible(true); tick();
    std::set<unsigned> fresh = models_updated();
    start(); set_visible(true); tick(); send_frame(frame(10.5, 0)); tick(); fake_updates.clear();
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
