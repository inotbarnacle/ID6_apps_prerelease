// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Uses NBT-AAD's HUDTerminal and MOST client. Copyright (c) 2026 nanaisu.
#include "cluster/hud_terminal.hpp"
#include "hud_sample.h"
#include "gauge_core.h"
#include "diagnostic_gateway.h"
#include "gauge_coexistence.h"
extern "C" {
#include "diag_link.h"
}
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace hud = nbt::hudterm;
namespace most = nbt::most;

#include "tach_digits.h"

static const unsigned frame = 1;
static const unsigned shape = 5;
static const unsigned stock = 6;
static const uint16_t expectedNode = 0x0101;
static volatile sig_atomic_t stop = 0;

static void stopSignal(int) { stop = 1; diag_running = 0; }

static void registry(const std::vector<most::RegistryEntry>& entries, void* user) {
    std::vector<most::RegistryEntry> matched;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].key == 0x8200)
            matched.push_back(entries[i]);
    }
    if (matched.size() != 1 || matched[0].address != expectedNode)
        matched.clear();
    static_cast<hud::HudTerminal*>(user)->onRegistry(matched);
}

static bool sendToExpectedNode(const most::Msg& message, void* user) {
    if (message.dst != expectedNode || projection_hud_installed()) return false;
    if (message.fkt == hud::kTextareaSetText && message.data.size() >= 2)
        printf("HUD send text %u: %s\n", hud::rdBe16(&message.data[0]),
               hud::decodeText(&message.data[0], message.data.size()).c_str());
    return static_cast<most::Client*>(user)->send(message);
}

static bool submit(hud::HudTerminal& terminal, const hud::Batch& batch) {
    if (terminal.submit(batch) != hud::kQueued || !terminal.waitIdle(8500)) {
        fprintf(stderr, "%s failed: %s\n", batch.label.c_str(), hud::statusName(terminal.status()));
        return false;
    }
    return true;
}

static const unsigned boostLabel = 2;
static const unsigned boostValue = 3;
static const unsigned unitsText = 4;

template <size_t Count>
static void digitHalf(hud::Batch& batch, unsigned buffer, unsigned index,
                      int x, int y, const hud::Point (&outline)[Count]) {
    std::vector<hud::Point> points(outline, outline + Count);
    for (size_t i = 0; i < points.size(); ++i) {
        points[i].x += x;
        points[i].y += y;
    }
    batch.add(hud::kAddPolygon, hud::addPolygon(buffer, index, hud::kPolygon, points));
}

static const unsigned rpmLimit = 8000;
static const unsigned redline = 6700;
static const int tachLeft = 0, tachRight = 116;

static int tachX(unsigned rpm) {
    if (rpm > rpmLimit) rpm = rpmLimit;
    return tachLeft + (tachRight - tachLeft) * rpm / rpmLimit;
}

static int arcHeight(int x) {
    const int offset = x - 70;
    return 19 + offset * offset / 220;
}

static hud::Point arcPoint(int x, int thickness) {
    const hud::Point point = {static_cast<int16_t>(x), static_cast<int16_t>(arcHeight(x) + thickness)};
    return point;
}

// One closed polygon per band keeps the complete frame to a handful of MOST calls.
static void tachBand(hud::Batch& batch, unsigned buffer, unsigned index,
                     int right, int thickness, int redlineThickness) {
    const int redlineX = tachX(redline);
    const int anchors[] = {tachLeft, 28, 49, 70, redlineX, tachRight};
    std::vector<hud::Point> points;
    for (unsigned i = 0; i < sizeof anchors / sizeof anchors[0] && anchors[i] < right; ++i)
        points.push_back(arcPoint(anchors[i], 0));
    points.push_back(arcPoint(right, 0));
    points.push_back(arcPoint(right, right > redlineX ? redlineThickness : thickness));
    for (int i = sizeof anchors / sizeof anchors[0] - 1; i >= 0; --i) {
        const int x = anchors[i];
        if (x >= right) continue;
        if (x == redlineX) points.push_back(arcPoint(x, redlineThickness));
        points.push_back(arcPoint(x, x > redlineX ? redlineThickness : thickness));
    }
    batch.add(hud::kAddPolygon, hud::addPolygon(buffer, index, hud::kPolygon, points));
}

static int shownBoost(const hud_sample& sample) {
    return sample.have_boost && (sample.boost_tenths < -1 || sample.boost_tenths > 1)
        ? sample.boost_tenths : 0;
}

static bool draw(hud::HudTerminal& terminal, const hud_sample& sample, unsigned& visible,
                 bool first, bool tachChanged, bool boostChanged) {
    const unsigned buffer = visible == stock ? shape : stock;
    hud::Batch batch(tachChanged ? "update HUD gauges" : "update boost text");
    if (first) {
        batch.add(hud::kSetFillColor, hud::setFillColor(shape, 4));
        batch.add(hud::kSetFillColor, hud::setFillColor(stock, 4));
        batch.add(hud::kSetVisibility, hud::setVisibility(boostLabel, false));
        batch.add(hud::kSetVisibility, hud::setVisibility(boostValue, true));
        batch.add(hud::kSetVisibility, hud::setVisibility(unitsText, true));
    }
    if (tachChanged) {
        batch.add(hud::kRemoveAllItems, hud::removeAllItems(buffer));
        tachBand(batch, buffer, 0, tachRight, 6, 11);
        const int fill = tachX(sample.rpm);
        if (fill > tachLeft) tachBand(batch, buffer, 1, fill, 17, 22);
        digitHalf(batch, buffer, 2, 13, 52, zeroLeft);
        digitHalf(batch, buffer, 3, 13, 52, zeroRight);
        digitHalf(batch, buffer, 4, 93, 52, eightLeft);
        digitHalf(batch, buffer, 5, 93, 52, eightRight);
        batch.add(hud::kSwitchVisibility, hud::switchVisibility(visible, buffer));
    }
    if (boostChanged) {
        char psi[16];
        if (sample.have_boost) snprintf(psi, sizeof psi, "%.1f", shownBoost(sample) / 10.0);
        else strcpy(psi, "--");
        batch.add(hud::kTextareaSetText, hud::setText(boostValue, psi));
    }
    if (first) batch.add(hud::kSetVisibility, hud::setVisibility(frame, true));
    if (terminal.submit(batch) != hud::kQueued) return false;
    if (tachChanged) visible = buffer;
    return true;
}

static bool clear(hud::HudTerminal& terminal, unsigned visible) {
    hud::Batch batch("hide and clear HUD gauges");
    batch.add(hud::kSetVisibility, hud::setVisibility(frame, false));
    if (visible != stock) batch.add(hud::kSwitchVisibility, hud::switchVisibility(visible, stock));
    batch.add(hud::kRemoveAllItems, hud::removeAllItems(shape));
    batch.add(hud::kRemoveAllItems, hud::removeAllItems(stock));
    batch.add(hud::kTextareaSetText, hud::setText(boostLabel, ""));
    batch.add(hud::kTextareaSetText, hud::setText(boostValue, ""));
    batch.add(hud::kTextareaSetText, hud::setText(unitsText, ""));
    batch.add(hud::kSetVisibility, hud::setVisibility(boostLabel, false));
    batch.add(hud::kSetVisibility, hud::setVisibility(boostValue, true));
    batch.add(hud::kSetVisibility, hud::setVisibility(unitsText, true));
    return submit(terminal, batch);
}

static bool dashboard_sample(int socket, unsigned source, hud_sample& sample) {
    bool updated = false;
    for (unsigned i = 0; i < 64; ++i) {
        struct hud_telemetry value;
        struct sockaddr_in peer;
        socklen_t size = sizeof peer;
        ssize_t count = recvfrom(socket, &value, sizeof value, 0,
                                 reinterpret_cast<sockaddr*>(&peer), &size);
        if (count < 0) break;
        const uint64_t now = bd_mono_ms();
        if (count != sizeof value || peer.sin_addr.s_addr != htonl(0x7f000001u) ||
            ntohs(peer.sin_port) != source || value.source != source ||
            value.sampled_ms > now || now - value.sampled_ms >= 1100 ||
            (updated && value.sampled_ms < sample.rpm_ms)) continue;
        sample.rpm = value.rpm;
        sample.rpm_ms = value.valid & HUD_SAMPLE_RPM ? value.sampled_ms : 0;
        sample.have_boost = (value.valid & HUD_SAMPLE_BOOST) != 0;
        sample.boost_tenths = value.boost_tenths;
        updated = true;
    }
    return updated;
}

static int trial(const char* host, bool requirePark) {
    if (projection_hud_installed()) return 7;
    // Hold the port through process exit, when the MOST client is destroyed by QNX.
    const int mostAccess = gauge_bind(GAUGE_MOST_PORT);
    if (mostAccess < 0 || gauge_active(BOOST_MOST_PORT) ||
        gauge_active(SHIFT_MOST_PORT) || gauge_active(LAUNCH_MOST_PORT) || gauge_active(WARM_MOST_PORT) ||
        gauge_active(DPF_MOST_PORT)) return 8;
    const int telemetry = gauge_bind(HUD_TELEMETRY_PORT);
    if (telemetry < 0) return 1;
    hud::Config config;
    config.handles.allow(frame);
    config.handles.allow(shape);
    config.handles.allow(stock);
    config.handles.allow(boostLabel);
    config.handles.allow(boostValue);
    config.handles.allow(unitsText);
    config.navFrame = frame;
    config.checkNav = true;
    config.navTexts.allow(boostLabel);
    config.navTexts.allow(boostValue);
    config.navTexts.allow(unitsText);
    config.navBuffers.allow(shape);
    config.navBuffers.allow(stock);
    config.maxInFlight = 3;
    config.maxBatch = 48;
    config.logBatches = true;

    // Process-owned objects live until _exit so DSI callbacks cannot outlive them.
    hud::HudTerminal& terminal = *new hud::HudTerminal(config);
    most::Client& client = *new most::Client;
    if (!terminal.attach(client) || !client.addRegistryListener(registry, &terminal)) return 1;
    terminal.setSender(sendToExpectedNode, &client);
    if (!terminal.start() || !client.start("HUD_SHAPE_TEST")) return 1;
    for (unsigned i = 0; i < 150 && !stop && terminal.status() == hud::kOffline; ++i)
        usleep(20000);
    if (stop || terminal.status() != hud::kReady) {
        terminal.logHealth();
        return 2;
    }

    bool shown = false, verified = false;
    unsigned source = 0;
    unsigned visible = stock;
    hud_reader reader;
    hud_reader_init(&reader);
    hud_sample latest = {}, submitted = {};
    uint64_t nextPoll = 0, nextConflictCheck = 0, lastTelemetry = bd_mono_ms();
    const pid_t parent = getppid();
    int result = 0;
    while (!stop && getppid() == parent) {
        if (bd_mono_ms() >= nextConflictCheck) {
            if (projection_hud_installed()) { result = 7; break; }
            nextConflictCheck = bd_mono_ms() + 500;
        }
        char request;
        if (recv(mostAccess, &request, 1, 0) > 0) { result = 8; break; }
        const hud::Status state = terminal.status();
        if (state == hud::kFailed) {
            result = 4;
            break;
        }
        if (state != hud::kReady) {
            hud_reader_close(&reader);
            latest.rpm_ms = 0;
            shown = verified = false;
            visible = stock;
            lastTelemetry = bd_mono_ms(); // pause the data timeout while factory guidance owns the HUD
            usleep(150000);
            continue;
        }

        uint64_t now = bd_mono_ms();
        if (now >= nextPoll) {
            const unsigned active = gauge_active(SHIFT_ACTIVE_PORT) ? SHIFT_ACTIVE_PORT :
                gauge_active(BOOST_ACTIVE_PORT) ? BOOST_ACTIVE_PORT :
                gauge_active(LAUNCH_ACTIVE_PORT) ? LAUNCH_ACTIVE_PORT :
                gauge_active(WARM_ACTIVE_PORT) ? WARM_ACTIVE_PORT : 0;
            if (active != source) {
                hud_reader_close(&reader);
                latest.rpm_ms = 0;
                source = active;
            }
            hud_sample sample = {};
            if (source) {
                // Dashboard workers own diagnostics until they exit. LaunchDeck has no RPM stream.
                if (!requirePark && (source == BOOST_ACTIVE_PORT || source == SHIFT_ACTIVE_PORT) &&
                    dashboard_sample(telemetry, source, sample)) latest = sample;
                lastTelemetry = now;
            } else {
                (void)dashboard_sample(telemetry, 0, sample); // Drain former producers.
                const int read = hud_reader_poll(&reader, host, &sample, requirePark);
                if (read > 0) { latest = sample; lastTelemetry = sample.rpm_ms; }
                else if (read < 0) latest.rpm_ms = 0;
            }
            now = bd_mono_ms();
            nextPoll = now + 30;
        }
        // The MOST client sends on its own thread. Sample during a redraw, then
        // use the newest sample as soon as the preceding frame is acknowledged.
        if (!stop && terminal.waitIdle(0)) {
            if (!latest.rpm_ms || now - latest.rpm_ms >= 1100) {
                if (shown) {
                    if (!clear(terminal, visible) && terminal.status() == hud::kReady) {
                        result = 5;
                        break;
                    }
                    shown = verified = false;
                    visible = stock;
                }
            } else if (!verified) {
                // Check the reference text handle before altering visibility or graphics.
                hud::Batch probe("check HUD layout");
                probe.add(hud::kTextareaSetText, hud::setText(unitsText, "PSI"));
                if (!submit(terminal, probe)) {
                    if (terminal.status() == hud::kFailed || terminal.status() == hud::kReady) {
                        result = 4;
                        break;
                    }
                } else verified = true;
            } else {
                const bool tachChanged = !shown || tachX(latest.rpm) != tachX(submitted.rpm);
                const bool boostChanged = !shown || latest.have_boost != submitted.have_boost ||
                                          shownBoost(latest) != shownBoost(submitted);
                if (tachChanged || boostChanged) {
                    if (draw(terminal, latest, visible, !shown, tachChanged, boostChanged)) {
                        shown = true;
                        submitted = latest;
                    } else if (terminal.status() == hud::kReady) {
                        result = 4;
                        break;
                    }
                }
            }
        }
        usleep(5000);
        if (now - lastTelemetry > 12000) break;
    }
    if (projection_hud_installed()) result = 7;
    if (!stop && !result && getppid() == parent) result = 6;
    hud_reader_close(&reader);
    close(telemetry);
    if (result && result != 6 && result != 8) return result;
    if (shown && terminal.status() == hud::kReady &&
        (!terminal.waitIdle(3500) || !clear(terminal, visible))) return 5;
    return result;
}

int main(int argc, char** argv) {
    if (argc != 3 || (strcmp(argv[2], "auto") && !diagnosticGatewayIPv4(argv[2]))) {
        fputs("Expected mode and a diagnostic gateway IPv4 address or auto\n", stderr);
        return 64;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, stopSignal);
    signal(SIGTERM, stopSignal);
    if (!strcmp(argv[1], "--check-parked")) {
        hud_sample sample;
        if (!hud_read(argv[2], &sample, true)) return 6;
        printf("RPM %u, boost available %d, boost %.1f PSI, gear %c\n",
               sample.rpm, sample.have_boost, sample.boost_tenths / 10.0, sample.gear);
        return 0;
    }
    const bool requirePark = strcmp(argv[1], "--draw-live") != 0;
    if (requirePark && strcmp(argv[1], "--draw-parked")) return 64;
    int rc = trial(argv[2], requirePark);
    fflush(NULL);
    _exit(rc);
}
