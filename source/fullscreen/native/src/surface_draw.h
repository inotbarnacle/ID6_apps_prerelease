#ifndef ID6_SURFACE_DRAW_H
#define ID6_SURFACE_DRAW_H

#include "surface_protocol.h"

enum canvas_channel_order { canvas_rgba, canvas_bgra };

struct canvas {
    unsigned char *buffer;
    int width, height, stride;
    enum canvas_channel_order order;
};

unsigned surface_tach(const struct surface_state *state);
void surface_draw(struct canvas canvas, const struct surface_state *state);

#endif
