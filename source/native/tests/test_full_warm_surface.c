#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
// Exercise the production rasterizer, including its actual glyph metrics.
#include "surface_draw.c"

static unsigned pixel(const unsigned char *buffer,unsigned width,unsigned height,unsigned x,unsigned y) {
    const unsigned char *p=buffer+((y*height/240)*width+x*width/640)*4;
    return (unsigned)p[0]<<16 | (unsigned)p[1]<<8 | p[2];
}

int main(void) {
    const unsigned panels[][2]={{800,480},{960,480},{1280,480},{1440,540},{1920,720}};
    struct surface_state state;
    memset(&state,0,sizeof state);
    state.magic=surface_magic;state.page=surface_warm;
    assert(sizeof state==496 && surface_packet_valid(&state,sizeof state));
    assert(!surface_packet_valid(&state,sizeof state-1));
    assert(!surface_packet_valid(&state,sizeof state+1));
    state.magic=0x49443653;assert(!surface_packet_valid(&state,sizeof state));
    state.magic=0x49443654;assert(!surface_packet_valid(&state,sizeof state));
    state.magic=0x49443655;assert(!surface_packet_valid(&state,sizeof state));
    state.magic=surface_magic;
    strcpy(state.values[0],"85.0 C");
    strcpy(state.values[1],"75.0 C");
    strcpy(state.values[2],"40.0 C");
    strcpy(state.values[3],"85 C target: pending");
    strcpy(state.values[4],"75 C target: met");
    strcpy(state.values[5],"40 C target: unknown");
    strcpy(state.status,"IDEmu simulated: Estimated time to app targets  30:00");
    assert(word_width(state.status,19)<=562);
    assert(word_width("App targets are advisory; follow vehicle guidance",15)<=562);
    for (unsigned i=0;i<3;++i) {
        state.boost_level[i]=1000;
        assert(word_width(state.values[i+3],15)<=164);
    }
    state.boost_signal[0]=surface_warm_pending;
    state.boost_signal[1]=surface_warm_met;
    state.boost_signal[2]=surface_warm_unknown;
    for (unsigned p=0;p<COUNT(panels);++p) {
        unsigned width=panels[p][0],height=panels[p][1];
        size_t size=(size_t)width*height*4;
        unsigned char *allocation=malloc(size+64);assert(allocation);
        memset(allocation,0xa5,size+64);
        struct canvas canvas={allocation+32,(int)width,(int)height,(int)width*4,canvas_rgba};
        surface_draw(canvas,&state);
        // The rounded 85.0 label must not turn a measured pending state green.
        assert(pixel(canvas.buffer,width,height,48,125)==accent);
        assert(pixel(canvas.buffer,width,height,247,125)==active);
        assert(pixel(canvas.buffer,width,height,446,125)==frame);
        state.boost_signal[2]=255; // malformed readiness never becomes met
        surface_draw(canvas,&state);
        assert(pixel(canvas.buffer,width,height,446,125)==frame);
        state.boost_signal[2]=surface_warm_unknown;
        for (unsigned i=0;i<32;++i) assert(allocation[i]==0xa5 && allocation[size+32+i]==0xa5);
        free(allocation);
    }
    puts("Full-screen Warm: explicit states, rounded pending value, protocol version, glyph bounds and five panels passed.");
    return 0;
}
