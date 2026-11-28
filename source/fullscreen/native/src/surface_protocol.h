#ifndef ID6_SURFACE_PROTOCOL_H
#define ID6_SURFACE_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

enum surface_page {
    surface_boost, surface_shift, surface_mhd, surface_codes, surface_road,
    surface_beamer, surface_launch, surface_warm,
    surface_home = 9, surface_updates, surface_system,
    surface_update_confirm, surface_install_confirm, surface_hidden
};
enum { surface_text_size = 36 };
// Version 4 retains Warm/Shift states and names all 58 Boost channels/empty slots.
enum { surface_magic = 0x49443656 };
enum { surface_warm_unknown, surface_warm_pending, surface_warm_met };
enum { surface_bm3=40, surface_mg, surface_xdrive, surface_hybrid };

static inline int companion_surface_page(unsigned page) {
    return page>=surface_bm3 && page<=surface_hybrid;
}

struct surface_state {
    uint32_t magic;
    uint32_t page;
    uint32_t selection;
    uint32_t launch_mode;
    uint32_t lamps;
    uint32_t road_mode;
    uint32_t road_distance;
    uint32_t road_lane;
    uint32_t road_altitude;
    uint32_t shift_target;
    uint32_t tach_visual; /* RPM plus one, or zero for the current sample. */
    char values[9][surface_text_size];
    char status[96];
    uint16_t boost_level[9]; /* Warm: progress. Shift: [0] display RPM, [1] RPM+1 (0 unknown). */
    uint8_t boost_signal[9]; /* Warm: states. Shift: [0] gasoline/diesel, [1] save failure. */
    uint8_t boost_style, boost_color, launch_run_state, warm_fahrenheit, road_has_scene;
};

/* The pipe delivers each state atomically even on hosts with the POSIX minimum. */
typedef char surface_state_wire_size[sizeof(struct surface_state) == 496 ? 1 : -1];

static inline int surface_packet_valid(const struct surface_state *state, size_t size) {
    return size==sizeof *state && state->magic==surface_magic;
}

#endif
