#include "surface_draw.h"
#include "surface_assets.h"
#include "library_font.h"
#include "mhd_logo.h"
#include "companion_logos.h"
#include "companions.h"
#include "road_jet_frame.h"
#include "launch_timer.h"
#include "surface_shift.h"
#include "dme_descriptions.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { logical_width = 640, logical_height = 240 };
enum { background = 0x050608, header = 0x101824, panel = 0x121a26,
       frame = 0x2f4055, focused_panel = 0x173754, accent = 0x72caff,
       bright = 0xf4f8ff, muted = 0xa8b8cc, active = 0x67e4a9 };

static unsigned canvas_color(struct canvas c, unsigned rgb)
{
    if (c.order == canvas_bgra)
        return ((rgb & 0xffu) << 16) | (rgb & 0xff00u) | ((rgb >> 16) & 0xffu);
    return rgb;
}

static void fill(struct canvas c, int x, int y, int w, int h, unsigned rgb)
{
    int left = x * c.width / logical_width, right = (x + w) * c.width / logical_width;
    int top = y * c.height / logical_height, bottom = (y + h) * c.height / logical_height;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > c.width) right = c.width;
    if (bottom > c.height) bottom = c.height;
    if (left >= right || top >= bottom) return;
    rgb = canvas_color(c, rgb);
    unsigned char *row = c.buffer + top * c.stride + left * 4;
    for (int column = left; column < right; ++column) {
        unsigned char *pixel = row + (column-left)*4;
        pixel[0] = rgb >> 16;
        pixel[1] = rgb >> 8;
        pixel[2] = rgb;
        pixel[3] = 255;
    }
    for (int line = top + 1; line < bottom; ++line)
        memcpy(c.buffer+line*c.stride+left*4,row,(right-left)*4);
}

static const struct library_glyph *glyph_for(int size, unsigned char ch)
{
    if (ch == 176) ch = 127;
    else if (ch < 32 || ch > 126) ch = '?';
    return (size >= 21 ? glyph_64 : glyph_24) + ch - 32;
}

static int artwork_height(struct canvas c)
{
    const int proportional_height = c.width * logical_height / logical_width;
    return proportional_height < c.height ? proportional_height : c.height;
}

static int word_width(const char *label, int size)
{
    int basis = size >= 21 ? 64 : 24;
    int width = 0;
    for (; *label; ++label) width += glyph_for(size,*label)->advance;
    return width*size/basis;
}

static void text(struct canvas c, int x, int y, const char *label, unsigned color, int size)
{
    const int basis = size >= 21 ? 64 : 24;
    const int rows = size >= 21 ? 72 : 29;
    const unsigned char *ink = size >= 21 ? ink_64 : ink_24;
    color = canvas_color(c, color);
    const unsigned channels[] = {color >> 16, (color >> 8) & 255, color & 255};
    int offset = x*basis;
    const int line_height = rows*size/basis;
    int top = y*c.height/logical_height;
    int bottom = (y+line_height)*c.height/logical_height;
    const int art_height = artwork_height(c);
    if (art_height < c.height) {
        const int height = line_height*art_height/logical_height;
        top += (bottom-top-height)/2;
        bottom = top+height;
    }
    for (; *label; ++label) {
        const struct library_glyph *g = glyph_for(size,*label);
        int left = offset*c.width/(basis*logical_width);
        int span = g->width*size*c.width/(basis*logical_width);
        offset += g->advance*size;
        if (left+span > c.width) break;
        if (left < 0 || top < 0 || bottom > c.height || !span || top == bottom) continue;
        for (int row=top; row<bottom; ++row) {
            const unsigned char *mask = ink+g->offset+((row-top)*rows/(bottom-top))*g->width;
            unsigned char *pixel = c.buffer+row*c.stride+left*4;
            for (int col=0; col<span; ++col, pixel+=4) {
                unsigned a = mask[col*g->width/span];
                if (!a) continue;
                for (unsigned k=0; k<3; ++k)
                    pixel[k] = (channels[k]*a+pixel[k]*(255-a)+127)/255;
            }
        }
    }
}

static void center(struct canvas c, int x, int y, const char *label, unsigned color, int size)
{
    text(c,x-word_width(label,size)/2,y,label,color,size);
}

static void rounded(struct canvas c, int x, int y, int w, int h, unsigned color)
{
    fill(c,x+3,y,w-6,1,color);
    fill(c,x+1,y+1,w-2,2,color);
    fill(c,x,y+3,w,h-6,color);
    fill(c,x+1,y+h-3,w-2,2,color);
    fill(c,x+3,y+h-1,w-6,1,color);
}

static void card(struct canvas c, int x, int y, int w, int h, bool selected)
{
    const int edge=selected ? 2 : 1;
    rounded(c,x,y,w,h,selected ? accent : frame);
    rounded(c,x+edge,y+edge,w-2*edge,h-2*edge,selected ? focused_panel : panel);
}

static const char *value(const struct surface_state *s, unsigned i)
{
    return s->values[i][0] ? s->values[i] : "--";
}

static void metric(struct canvas c, int x, int y, int w, const char *label, const char *reading, int size)
{
    card(c,x,y,w,53,false);
    text(c,x+10,y+5,label,muted,13);
    text(c,x+10,y+22,reading,bright,size);
}

static void button(struct canvas c, int x, int y, int w, int h, const char *label, bool selected)
{
    card(c,x,y,w,h,selected);
    if (selected) fill(c,x+5,y+6,2,h-12,accent);
    text(c,x+12,y+(h-16)/2,label,bright,15);
}

#define COUNT(array) (sizeof(array)/sizeof(*(array)))
static void buttons(struct canvas c, const struct feature_button *items, unsigned count, unsigned selection)
{
    for (unsigned i=0; i<count; ++i) {
        const struct feature_button *b = &items[i];
        button(c,b->x,b->y,b->width,b->height,b->title,(unsigned)b->action == selection);
    }
}

static void titlebar(struct canvas c, const char *title)
{
    fill(c,0,0,logical_width,34,header);
    fill(c,0,34,logical_width,1,frame);
    fill(c,14,9,3,17,accent);
    text(c,24,3,title,bright,25);
}

static void draw_wordmark(struct canvas c, const unsigned char *rgba, int source_width, int source_height)
{
    const int logo_width=source_width/2, logo_height=source_height/2;
    const int left=(616-logo_width)*c.width/logical_width;
    const int width=logo_width*c.width/logical_width;
    const int height=logo_height*artwork_height(c)/logical_height;
    const int top=(34-logo_height)*c.height/(2*logical_height)+
                  (logo_height*c.height/logical_height-height)/2;
    const unsigned red=c.order==canvas_bgra ? 2u : 0u, blue=2u-red;
    for (int y=0;y<height;++y) {
        const unsigned char *source=rgba+(y*source_height/height)*source_width*4;
        unsigned char *pixel=c.buffer+(top+y)*c.stride+left*4;
        for (int x=0;x<width;++x,pixel+=4) {
            const unsigned char *src=source+(x*source_width/width)*4;
            const unsigned alpha=src[3];
            if (!alpha) continue;
            pixel[red]=(src[0]*alpha+pixel[red]*(255-alpha)+127)/255;
            pixel[1]=(src[1]*alpha+pixel[1]*(255-alpha)+127)/255;
            pixel[blue]=(src[2]*alpha+pixel[blue]*(255-alpha)+127)/255;
        }
    }
}

static void map_card(struct canvas c, int x, int y, int width, int height, unsigned label,
                     const char *tag, bool focused)
{
    card(c,x,y,width,height,focused);
    if (focused) fill(c,x+6,y+8,2,height-16,accent);
    char number[8];
    snprintf(number,sizeof number,"%u",label);
    center(c,x+width/2,y+4,number,bright,33);
    center(c,x+width/2,y+46,tag,focused ? bright : muted,15);
}

static void draw_companion(struct canvas c, const struct surface_state *state)
{
    const struct companion_page *page=companion_lookup(state->page,state->launch_mode);
    if (!page) return;
    titlebar(c,page->title);
    if (state->page==surface_mg)
        draw_wordmark(c,mg_flasher_rgba,mg_flasher_width,mg_flasher_height);
    if (state->page==surface_bm3)
        draw_wordmark(c,bootmod3_rgba,bootmod3_width,bootmod3_height);
    if (state->page==surface_hybrid) {
        static const char *const labels[]={"Battery charge","Pack voltage","Pack current","Pack temp",
                                          "Engine coolant","Module voltage","Battery health","Pack power"};
        for (unsigned i=0;i<COUNT(labels);++i)
            metric(c,24+150*(i%4),48+61*(i/4),142,labels[i],value(state,i),22);
        if (page->count) button(c,468,177,148,28,companion_buttons[page->first].label,
                                state->selection==companion_buttons[page->first].select);
    } else if (page->maps) {
        const int width=(590-10*((int)page->maps-1))/(int)page->maps;
        const bool mg=state->page==surface_mg;
        for (unsigned i=0;i<page->maps;++i) {
            const struct companion_button *control=&companion_buttons[page->first+i];
            map_card(c,26+i*(width+10),45,width,69,mg ? i : i+1,mg ? "SLOT" : "MAP",
                     state->selection==control->select);
        }
        const int columns=mg ? 4 : 3;
        const int control_width=mg ? 140 : 188;
        const int control_gap=mg ? 10 : 8;
        const int control_height=mg ? 22 : 26;
        for (unsigned i=page->maps;i<page->count;++i) {
            const struct companion_button *control=&companion_buttons[page->first+i];
            const int index=(int)(i-page->maps);
            const int x=26+(index%columns)*(control_width+control_gap);
            const int width=(index%columns)==columns-1 ? 616-x : control_width;
            button(c,x,123+25*(index/columns),width,control_height,control->label,
                   state->selection==control->select);
        }
    } else {
        enum { per_screen=12 };
        unsigned first=0;
        for (unsigned i=0;i<page->count;++i)
            if (companion_buttons[page->first+i].select==state->selection) {
                first=i/per_screen*per_screen;
                break;
            }
        for (unsigned i=0;i<per_screen && first+i<page->count;++i) {
            const struct companion_button *control=&companion_buttons[page->first+first+i];
            const bool focused=control->select==state->selection;
            const int x=24+301*(i%2), y=47+25*(i/2);
            card(c,x,y,291,22,focused);
            if (focused) fill(c,x+5,y+5,2,12,accent);
            text(c,x+12,y+3,control->label,bright,word_width(control->label,15)>269 ? 12 : 15);
        }
        if (page->count>per_screen) {
            char counter[20];
            snprintf(counter,sizeof counter,"%u / %u",1+first/per_screen,(page->count+per_screen-1)/per_screen);
            text(c,575,207,counter,muted,12);
        }
    }
    if (page->hint[0])
        text(c,24,201,page->hint,muted,word_width(page->hint,13)>540 ? 11 : 13);
    if (state->status[0])
        text(c,24,218,state->status,bright,word_width(state->status,14)>590 ? 11 : 14);
}

static void hub(struct canvas c, const struct surface_state *s)
{
    const bool system = s->page == surface_system;
    titlebar(c,"ID6 App Updates");
    fill(c,98,43,1,177,frame);
    button(c,12,53,72,37,"Updates",s->selection == surface_updates);
    button(c,12,103,72,37,"System",s->selection == surface_system);
    fill(c,12,system ? 104 : 54,3,35,accent);
    if (s->page >= surface_update_confirm) {
        bool install = s->page == surface_install_confirm;
        text(c,121,47,install ? "Install new apps" : "Update installed apps",bright,24);
        text(c,121,87,install ? "Add compatible missing apps to My Vehicle." :
                                "Refresh matching apps already on this iDrive.",muted,17);
        button(c,121,124,225,34,"Confirm",true);
        text(c,121,180,s->status[0] ? s->status : "Press Back to return to Updates.",bright,16);
        return;
    }
    if (s->page == surface_updates) {
        text(c,121,45,"USB packages",bright,23);
        buttons(c,usb_controls,COUNT(usb_controls),s->selection);
        text(c,131,113,"Refresh apps already installed.",muted,13);
        text(c,380,113,"Add supported apps to My Vehicle.",muted,13);
        fill(c,121,151,495,1,frame);
        text(c,121,158,s->status[0] ? s->status : "Use ID6Apps.update at the root of the USB drive.",bright,16);
        return;
    }
    text(c,121,47,"About ID6 Apps",bright,24);
    metric(c,121,88,495,"Vehicle data","An app uses diagnostics while its page is open.",19);
    metric(c,121,152,495,"USB installation","Installed apps appear in My Vehicle after iDrive restarts.",17);
}

unsigned surface_tach(const struct surface_state *s)
{
    return surface_shift_sample(s);
}

static void tachometer(struct canvas c, unsigned rpm, unsigned ceiling, unsigned target, bool diesel, int y, int height)
{
    for (int i=0; i<40; ++i) {
        int x=25+i*14, h=height-8+i/4;
        unsigned color = i>33 ? 0xff4b50 : i>27 ? 0xf4a941 : i>16 ? 0x647bff : 0x22adfa;
        unsigned step = (unsigned)(i+1)*ceiling/40;
        if (diesel) color=step>=target ? 0xff4b50 : step+1000>target ? 0xf4a941 : 0x647bff;
        if (!ceiling || rpm < step) color = color==0xff4b50 ? 0x432124 : 0x1b222d;
        for (int row=0; row<h; ++row)
            fill(c,x+(h-row)/6,y+height-row,10,1,color);
    }
    if (ceiling) {
        const unsigned marks=ceiling/1000;
        for (unsigned i=0; i<=marks; ++i) {
            char number[] = {(char)('0'+i),0};
            text(c,25+(int)(i*552/marks),y+height+5,number,muted,11);
        }
        text(c,584,y+height+5,"x1000",muted,11);
    }
}

static void draw_shift(struct canvas c, const struct surface_state *s)
{
    const bool valid=surface_shift_valid(s)!=0;
    unsigned sample = surface_tach(s);
    unsigned visual = sample && s->tach_visual && s->tach_visual<=20001 ? s->tach_visual : sample;
    unsigned rpm = visual ? visual-1 : 0;
    for (int i=0; i<12; ++i) {
        unsigned color = i>8 ? 0xff4b50 : i>5 ? 0xffb34a : active;
        fill(c,138+31*i,51,24,6,surface_shift_light(s,(unsigned)i) ? color : frame);
    }
    tachometer(c,rpm,valid ? s->boost_level[0] : 0,s->shift_target,valid && s->boost_signal[0],140,39);
    char digits[16] = "--";
    if (sample) snprintf(digits,sizeof digits,"%u",(sample-1+5u)/10u*10u);
    center(c,320,61,digits,bright,53);
    center(c,320,120,valid && s->boost_signal[0] ? "DIESEL RPM" : "ENGINE RPM",muted,13);
    char target_text[24];
    if (valid) snprintf(target_text,sizeof target_text,"Target %u RPM",s->shift_target);
    else snprintf(target_text,sizeof target_text,"Settings unavailable");
    button(c,479,4,138,24,target_text,s->selection==216);
    metric(c,24,77,134,"ROAD SPEED",valid ? value(s,0) : "--",24);
    metric(c,483,77,133,"GEAR",valid ? value(s,2) : "--",24);
    center(c,320,226,"App target; follow vehicle guidance",muted,11);
}

static const char *const boost_names[][2] = {
#define SIGNAL(id, name, lo, hi, unit, wide, narrow) {wide,narrow},
#include "boost_signals.def"
#undef SIGNAL
};

static void dial(struct canvas c, int cx, int cy, unsigned level, unsigned color)
{
    static const signed char points[][2] = {
        {-24,18},{-27,13},{-29,7},{-30,1},{-29,-5},{-28,-11},{-25,-17},
        {-21,-22},{-16,-26},{-11,-28},{-5,-30},{0,-30},{5,-30},{11,-28},
        {16,-26},{21,-22},{25,-17},{28,-11},{29,-5},{30,1},{29,7},{27,13},{24,18}
    };
    for (unsigned i=0;i<COUNT(points);++i) {
        unsigned tint=(i+1)*1000<=level*COUNT(points) ? color : frame;
        fill(c,cx+points[i][0]-2,cy+points[i][1]-2,5,5,tint);
    }
}

static void draw_boost(struct canvas c, const struct surface_state *s)
{
    static const unsigned palette[] = {
        0xff6431,0x4baaff,0x60d697,0xf2f5fa,0x9b77f3,0xff86c5,0x42cfd1,0xff4b50
    };
    button(c,442,5,83,24,"Settings",s->selection==214);
    for (unsigned i=0;i<9;++i) {
        bool column=i==8;
        int x=column ? 537 : 24+(i/2)*128;
        int y=column ? 44 : 44+(i%2)*81;
        int w=column ? 79 : 120, h=column ? 156 : 75;
        unsigned color=palette[s->boost_color<COUNT(palette) ? s->boost_color : i%COUNT(palette)];
        const unsigned signal=s->boost_signal[i];
        const char *label=signal<COUNT(boost_names) ? boost_names[signal][column] : "--";
        unsigned level=s->boost_level[i]<=1000 ? s->boost_level[i] : 0;
        card(c,x,y,w,h,false);
        if (s->boost_style) {
            int center_x=x+w/2, center_y=y+(column ? 75 : 44);
            center(c,center_x,y+3,label,muted,15);
            dial(c,center_x,center_y,level,color);
            const char *reading=value(s,i);
            center(c,center_x,center_y-8,reading,bright,word_width(reading,18)>55 ? 12 : 18);
        } else {
            text(c,x+9,y+5,label,muted,17);
            if (column) {
                rounded(c,x+28,y+29,22,95,frame);
                int height=(int)(95*level/1000);
                if (height>=6) rounded(c,x+28,y+124-height,22,height,color);
                else if (height) fill(c,x+28,y+124-height,22,height,color);
                center(c,x+w/2,y+126,value(s,i),bright,22);
            } else {
                text(c,x+9,y+25,value(s,i),bright,24);
                rounded(c,x+9,y+58,w-18,6,frame);
                int width=(int)((w-18)*level/1000);
                if (width>=6) rounded(c,x+9,y+58,width,6,color);
                else if (width) fill(c,x+9,y+58,width,6,color);
            }
        }
    }
}

static void draw_warm(struct canvas c, const struct surface_state *s)
{
    static const char *const names[] = {"Engine oil","Coolant","Transmission"};
    for (unsigned i=0;i<COUNT(names);++i) {
        int x=24+i*199;
        card(c,x,46,192,115,false);
        text(c,x+14,55,names[i],muted,20);
        text(c,x+14,82,value(s,i),bright,30);
        const bool known=s->boost_signal[i]==surface_warm_pending || s->boost_signal[i]==surface_warm_met;
        const bool reached=known && s->boost_signal[i]==surface_warm_met;
        rounded(c,x+14,122,163,6,frame);
        unsigned level=s->boost_level[i]>1000 ? 1000 : s->boost_level[i];
        int width=known ? (int)(163*level/1000) : 0;
        if (width>=6) rounded(c,x+14,122,width,6,reached ? active : accent);
        text(c,x+14,133,value(s,i+3),!known ? muted : reached ? active : accent,15);
    }
    card(c,24,169,590,33,false);
    text(c,38,174,s->status,bright,19);
    text(c,38,212,"App targets are advisory; follow vehicle guidance",muted,15);
}

static void segment(struct canvas c, int x1, int y1, int x2, int y2, unsigned tint)
{
    int dx=abs(x2-x1), dy=abs(y2-y1), steps=dx>dy ? dx : dy;
    if (!steps) return;
    for (int i=0;i<=steps;++i)
        fill(c,x1+(x2-x1)*i/steps,y1+(y2-y1)*i/steps,2,2,tint);
}

static void draw_beamer(struct canvas c, const struct surface_state *s)
{
    static const int body[][2]={{0,50},{7,36},{23,29},{44,10},{130,10},{151,29},{166,36},{172,50},{166,64},{7,64},{0,50}};
    for (unsigned i=0;i<2;++i) {
        int x=24+i*300;
        card(c,x,44,290,101,false);
        for (unsigned j=1;j<COUNT(body);++j)
            segment(c,x+57+body[j-1][0],48+body[j-1][1],x+57+body[j][0],48+body[j][1],0x536275);
        segment(c,x+82,79,x+202,79,0x536275);
        bool lamp=(s->lamps & (i ? 12u : 3u))!=0;
        unsigned tint=lamp ? 0xffedc7 : 0x536275;
        segment(c,x+72,91,x+104,96,tint);
        segment(c,x+181,96,x+215,91,tint);
        if (i) {
            fill(c,x+124,91,40,2,frame);
            fill(c,x+124,101,40,2,frame);
        } else {
            fill(c,x+129,91,12,12,frame);
            fill(c,x+146,91,12,12,frame);
        }
        center(c,x+145,121,i ? "Rear" : "Front",muted,16);
    }
    buttons(c,beamer_controls,COUNT(beamer_controls),s->selection);
}

static void draw_road(struct canvas c, const struct surface_state *state)
{
    const bool playing=state->road_mode==ROAD_JET_ACTIVE || state->road_mode==ROAD_JET_PEDAL_ONLY;
    if (!playing && !state->road_has_scene) {
        center(c,320,91,"Ready at 5 mph or less",bright,31);
        return;
    }

    const int horizon = 58, road_end = 197;
    const unsigned distance = state->road_distance % 3600000u;
    const unsigned scroll = distance % 72000u;
    const unsigned char obstacles[] = {1,3,2,0,4,3};
    const unsigned section = distance/600000u;
    const unsigned obstacle_step = distance%600000u/16000u;
    fill(c,26,39,590,165,0x171c34);
    fill(c,26,horizon,590,147,0x302342);
    for (int y=horizon; y<road_end; ++y) {
        int depth = y-horizon;
        int half = 24 + depth*2;
        fill(c,320-half,y,half*2,1,0x262638);
        fill(c,320-half,y,2,1,accent);
        fill(c,320+half-2,y,2,1,accent);
        if ((depth*1000u+scroll)%14000u < 6800u) {
            for (int lane=1; lane<5; ++lane)
                fill(c,320-half+lane*half*2/5,y,1+depth/64,1,frame);
        }
    }
    if (obstacle_step < 26) {
        int depth = 8+(int)obstacle_step*5;
        int half = 24+depth*2;
        int x = 320-half+(int)(2u*obstacles[section]+1u)*half/5;
        int radius = 2+depth/14;
        fill(c,x-radius,horizon+depth-radius,2*radius,2*radius,0xff946b);
    }
    int lane = state->road_lane < 5 ? (int)state->road_lane : 2;
    int altitude = state->road_altitude <= 1000 ? (int)state->road_altitude : 1000;
    int x = 320 + (lane-2)*103, y = 183 - altitude*73/1000;
    fill(c,x-13,192,26,4,0x171422);
    fill(c,x-20,y+5,40,5,accent);
    fill(c,x-9,y-5,18,15,0xb8dbff);
    fill(c,x-5,y-9,10,7,bright);
    fill(c,x-5,y+10,10,4,0xff946b);
    if (!playing) {
        card(c,196,82,248,48,false);
        center(c,320,91,"Paused",bright,24);
    }
}

void surface_draw(struct canvas c, const struct surface_state *s)
{
    fill(c,0,0,logical_width,logical_height,background);
    if (companion_surface_page(s->page)) { draw_companion(c,s); return; }
    if (s->page >= surface_hidden) return;
    if (s->page >= surface_home) { hub(c,s); return; }
    titlebar(c,surface_titles[s->page]);
    switch (s->page) {
    case surface_shift: draw_shift(c,s); break;
    case surface_boost: draw_boost(c,s); break;
    case surface_warm: draw_warm(c,s); break;
    case surface_beamer: draw_beamer(c,s); break;
    case surface_road: draw_road(c,s); break;
    case surface_mhd:
        draw_wordmark(c,mhd_logo_rgba,mhd_logo_width,mhd_logo_height);
        for (unsigned i=0;i<4;++i) {
            const struct feature_button *b=&mhd_controls[i];
            const bool focused=s->selection==(unsigned)b->action;
            map_card(c,b->x,b->y,b->width,b->height,i+1,"MAP",focused);
        }
        buttons(c,mhd_controls+4,COUNT(mhd_controls)-4,s->selection);
        text(c,26,187,value(s,0),muted,15);
        break;
    case surface_codes:
        if (s->selection==234 || s->selection==236) {
            card(c,24,48,592,147,false);
            text(c,42,58,"Clear engine codes?",bright,24);
            text(c,42,92,"Erases stored codes and freeze-frame data.",muted,17);
            text(c,42,115,"Park with ignition on and engine off.",muted,17);
            button(c,72,146,170,35,"Cancel",s->selection==236);
            button(c,325,146,220,35,"Confirm clear",s->selection==234);
            break;
        }
        for (unsigned i=0; i<4; ++i) {
            if (!s->values[i][0]) continue;
            int y=41+i*37;
            card(c,24,y,592,34,false);
            unsigned code, flag;
            if (sscanf(s->values[i],"%6x  status %2x",&code,&flag)==2) {
                char label[32];
                snprintf(label,sizeof label,"%06X  %s",code,flag&1u ? "Active" : flag&4u ? "Pending" : "Stored");
                text(c,36,y,label,bright,16);
                const char *description=dme_code_description(code);
                text(c,36,y+16,description,muted,word_width(description,15)>558 ? 12 : 15);
            } else text(c,36,y,s->values[i],bright,16);
        }
        button(c,331,194,130,29,"Next codes  >",s->selection==230);
        button(c,490,194,128,29,"Clear codes",s->selection==232);
        break;
    case surface_launch: {
        static const char *const modes[] = {"0-60 mph","60-130 mph","0-100 km/h","100-200 km/h"};
        card(c,24,43,592,153,false);
        center(c,320,46,s->launch_run_state==LT_ARMED ? "READY" :
                        s->launch_run_state==LT_RUNNING ? "TIMING" :
                        s->launch_run_state==LT_DONE ? "RUN COMPLETE" : "WAITING",
               s->launch_run_state==LT_ARMED ? active : muted,17);
        text(c,59,78,modes[s->launch_mode<4 ? s->launch_mode : 2],accent,19);
        center(c,320,65,!strcmp(value(s,1),"Ready") ? "0.00" : value(s,1),bright,62);
        fill(c,39,141,562,1,frame);
        static const char *const labels[]={"Live speed","Previous 1","Previous 2","Previous 3"};
        for (unsigned i=0;i<4;++i) {
            int x=39+i*142;
            rounded(c,x,148,134,43,0x1a222d);
            center(c,x+67,150,labels[i],muted,13);
            center(c,x+67,166,value(s,i ? i+1 : 0),bright,20);
        }
        button(c,458,199,160,29,"Change mode  >",s->selection==231);
        break;
    }
    default: break;
    }
    if (s->status[0] && s->page!=surface_warm) text(c,26,211,s->status,muted,13);
}
