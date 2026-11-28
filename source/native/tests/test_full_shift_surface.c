#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "surface_draw.c"

static unsigned pixel(struct canvas c,unsigned x,unsigned y) {
    const unsigned char *p=c.buffer+((y*(unsigned)c.height/240)*(unsigned)c.width+x*(unsigned)c.width/640)*4;
    return (unsigned)p[0]<<16 | (unsigned)p[1]<<8 | p[2];
}

int main(void) {
    const unsigned panels[][2]={{800,480},{960,480},{1280,480},{1440,540},{1920,720}};
    struct surface_state state;
    memset(&state,0,sizeof state);
    state.magic=surface_magic;state.page=surface_shift;
    state.boost_level[0]=8000;state.shift_target=6500;
    strcpy(state.values[0],"60 km/h");strcpy(state.values[2],"--");
    assert(surface_packet_valid(&state,sizeof state) && surface_shift_valid(&state));
    // Rounded display text is deliberately misleading; raw RPM decides lights.
    strcpy(state.values[1],"6500");state.boost_level[1]=6500; // 6499 RPM
    assert(surface_tach(&state)==6500 && !surface_shift_light(&state,11));
    state.boost_level[1]=6501;assert(surface_shift_light(&state,11));
    state.boost_level[1]=1;assert(surface_tach(&state)==1); // valid zero
    state.boost_level[1]=0;assert(!surface_tach(&state)); // unknown
    state.boost_level[1]=20002;assert(!surface_tach(&state));
    for (unsigned target=6000;target<=8000;target+=100) {
        state.shift_target=target;
        for (unsigned light=0;light<12;++light) {
            const unsigned trigger=target-(11-light)*200;
            state.boost_level[1]=(uint16_t)trigger;
            assert(!surface_shift_light(&state,light));
            ++state.boost_level[1];assert(surface_shift_light(&state,light));
        }
    }
    assert(word_width("App target; follow vehicle guidance",11)<600);
    for (unsigned panel_index=0;panel_index<COUNT(panels);++panel_index) {
        const unsigned width=panels[panel_index][0],height=panels[panel_index][1];
        const size_t size=(size_t)width*height*4;
        unsigned char *allocation=malloc(size+64);assert(allocation);
        memset(allocation,0xa5,size+64);
        struct canvas canvas={allocation+32,(int)width,(int)height,(int)width*4,canvas_rgba};
        for (unsigned ceiling=4000;ceiling<=6000;ceiling+=1000) {
            state.boost_signal[0]=1;state.boost_level[0]=(uint16_t)ceiling;
            state.shift_target=ceiling-100;state.boost_level[1]=(uint16_t)(ceiling/2+1);
            surface_draw(canvas,&state);
            assert(surface_shift_valid(&state));
            // The same measured fraction lights exactly half of each selected bar.
            assert(pixel(canvas,298,179)==0x647bff);
            assert(pixel(canvas,312,179)==0x1b222d);
            assert(pixel(canvas,138,53)==frame);
            state.boost_level[1]=(uint16_t)(state.shift_target+1);
            surface_draw(canvas,&state);
            assert(pixel(canvas,138,53)==active && pixel(canvas,479,53)==0xff4b50);
            // Expired RPM cannot inherit an eased visual sample or display text.
            state.boost_level[1]=0;state.tach_visual=6001;
            surface_draw(canvas,&state);
            assert(pixel(canvas,138,53)==frame && pixel(canvas,479,53)==frame);
            assert(pixel(canvas,32,179)==0x1b222d);
            state.tach_visual=0;
        }
        state.boost_level[0]=5000;state.shift_target=5000;
        state.boost_level[1]=6001;
        assert(!surface_shift_valid(&state));
        surface_draw(canvas,&state);
        assert(pixel(canvas,138,53)==frame && pixel(canvas,32,179)==0x1b222d);
        state.shift_target=4000;
        for (unsigned old=0x49443653;old<=0x49443654;++old) {
            state.magic=old;
            assert(!surface_packet_valid(&state,sizeof state) && !surface_tach(&state));
        }
        state.magic=surface_magic;
        for (unsigned i=0;i<32;++i) assert(allocation[i]==0xa5 && allocation[size+32+i]==0xa5);
        free(allocation);
    }
    puts("Full-screen Shift: raw RPM, all gasoline targets, three diesel scales, stale lights, invalid settings and five panels passed.");
    return 0;
}
