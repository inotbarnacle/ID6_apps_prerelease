#include "surface_animation.h"
#include <string.h>

unsigned overlay_state_size(void)
{
    return sizeof(struct surface_state);
}

unsigned overlay_hidden_page(void) { return surface_hidden; }
unsigned overlay_animation_size(void) { return sizeof(struct surface_animation); }

unsigned overlay_frame(void *animation, const void *packet, void *output, uint64_t now)
{
    struct surface_state state;
    memcpy(&state,packet,sizeof state);
    struct surface_animation *a = animation;
    tach_animation_update(a,&state,now);
    state.tach_visual = tach_animation_value(a,now);
    memcpy(output,&state,sizeof state);
    return state.tach_visual != a->target;
}

void overlay_render(const void *packet, unsigned char *rgba, unsigned char *rgb, int w, int h)
{
    struct surface_state state;
    memcpy(&state,packet,sizeof state);
    state.status[sizeof state.status-1] = 0;
    for (unsigned i=0;i<9;++i) state.values[i][sizeof state.values[i]-1] = 0;
    struct canvas canvas = {rgba,w,h,w*4,canvas_rgba};
    surface_draw(canvas,&state);
    for (int i=0; i<w*h; ++i) {
        *rgb++ = *rgba++;
        *rgb++ = *rgba++;
        *rgb++ = *rgba++;
        ++rgba;
    }
}
