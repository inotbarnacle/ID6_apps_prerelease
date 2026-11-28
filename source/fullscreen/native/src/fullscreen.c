/* Private QNX Screen surface. An inherited pipe carries state from RHMI. */
#include "screen_video.h"
#include "surface_animation.h"
#include "layer_manager.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static void stop(int signum) { (void)signum; running = 0; }

static uint64_t monotonic_ms(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC,&time);
    return (uint64_t)time.tv_sec*1000u + time.tv_nsec/1000000u;
}

static bool safe_size(int width, int height)
{
    return (height==480 && (width==800 || width==960 || width==1280)) ||
           (height==540 && width==1440) || (height==720 && width==1920);
}

static void clean_state(struct surface_state *state)
{
    state->status[sizeof state->status-1] = 0;
    for (unsigned i=0; i<9; ++i) state->values[i][sizeof state->values[i]-1] = 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
#ifdef __QNX__
    if (!strncmp(argv[0],"/var/opt/hmi/data/ID5/Plugins/ID6LibraryLab/",43)) {
        const char trace[] = "/dev/shmem/ID6LibraryLab-screen.log";
        int out = open(trace,O_WRONLY | O_CREAT | O_APPEND,0600);
        if (out >= 0) { dup2(out,STDERR_FILENO); close(out); }
        fprintf(stderr,"start %ld parent %ld\n",(long)getpid(),(long)getppid());
    }
#endif
    char *end;
    long width = strtol(argv[1], &end, 10);
    if (*end || width < 800 || width > 1920) return 2;
    long height = strtol(argv[2], &end, 10);
    if (*end || height < 480 || height > 720 || !safe_size((int)width,(int)height)) return 2;
    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    int result = 1, shown = 0, format = SCREEN_FORMAT_RGBA8888;
    int usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_WRITE;
    const char group[] = "ID6LibraryFullscreen";
    const char name[] = LIBRARY_SCREEN_ID;
    const bool use_layer_manager = layer_manager_available();
    bool attached = false;
    const char *stage = "display discovery";
    unsigned retries = 0;
    screen_context_t context = 0;
    screen_window_t window = 0;
    screen_buffer_t buffers[2] = {0,0};
    void *pixels[2] = {0,0};
    int strides[2] = {(int)width*4,(int)width*4};
    int (*read_buffer_int)(screen_buffer_t,int,int*) = NULL;
    void *screen_library = NULL;
    if (use_layer_manager) {
        int measured_width, measured_height;
        if (layer_manager_surface_bound()) { fprintf(stderr,"Surface ID already bound\n"); return 1; }
        if (layer_manager_size(&measured_width,&measured_height)) {
            if (!safe_size(measured_width,measured_height)) return 2;
            width = measured_width;
            height = measured_height;
        }
    }
    int size[2] = {(int)width,(int)height}, pos[2] = {0,0};
    int z = NBT_SCREEN_ZORDER_ACTIVE, rect[4] = {0,0,(int)width,(int)height};
    stage = "screen context or window";
    if (screen_create_context(&context, SCREEN_APPLICATION_CONTEXT) != 0 ||
        screen_create_window_type(&window, context, SCREEN_APPLICATION_WINDOW) != 0) goto cleanup;

    int display_count = 0;
    if (!screen_get_context_property_iv(context, SCREEN_PROPERTY_DISPLAY_COUNT, &display_count) &&
        display_count > 0 && display_count <= 16) {
        screen_display_t displays[16] = {0};
        if (!screen_get_context_property_pv(context, SCREEN_PROPERTY_DISPLAYS, displays) && displays[0] &&
            screen_set_window_property_pv(window, SCREEN_PROPERTY_DISPLAY, displays) != 0) goto cleanup;
    }
    stage = "window properties or buffers";
    /* LayerManager owns composition here. Changing the Screen visibility or
       z-order as well can make the window fight RHMI for the front plane. */
    if ((!use_layer_manager && screen_set_window_property_iv(window,SCREEN_PROPERTY_VISIBLE,&shown) != 0) ||
        screen_create_window_group(window,group) != 0 ||
        screen_set_window_property_cv(window, SCREEN_PROPERTY_ID_STRING, sizeof name-1, name) != 0 ||
        screen_set_window_property_iv(window, SCREEN_PROPERTY_USAGE, &usage) != 0 ||
        screen_set_window_property_iv(window, SCREEN_PROPERTY_FORMAT, &format) != 0 ||
        screen_set_window_property_iv(window, SCREEN_PROPERTY_POSITION, pos) != 0 ||
        screen_set_window_property_iv(window, SCREEN_PROPERTY_SOURCE_SIZE, size) != 0 ||
        (!use_layer_manager && screen_set_window_property_iv(window,SCREEN_PROPERTY_ZORDER,&z) != 0) ||
        screen_create_window_buffers(window,2) != 0 ||
        screen_get_window_property_pv(window, SCREEN_PROPERTY_RENDER_BUFFERS, buffers) != 0) goto cleanup;

    stage = "buffer pointer";
    screen_library = dlopen("libscreen.so.1",RTLD_LAZY | RTLD_LOCAL);
    if (screen_library) read_buffer_int = (int (*)(screen_buffer_t,int,int*))
        dlsym(screen_library,"screen_get_buffer_property_iv");
    for (unsigned i=0; i<2; ++i) {
        if (!buffers[i] || screen_get_buffer_property_pv(buffers[i],SCREEN_PROPERTY_POINTER,&pixels[i]) != 0 ||
            !pixels[i]) goto cleanup;
        /* Older OMAP Screen exposes the CPU pointer but rejects the stride query.
           Its native RGBA buffers use the same packed stride as NBT-AAD's OSD. */
        strides[i] = (int)width*4;
        int measured = 0;
        if (read_buffer_int && !read_buffer_int(buffers[i],SCREEN_PROPERTY_STRIDE,&measured) &&
            measured >= strides[i]) strides[i] = measured;
    }

    result = 0;
    unsigned index = 0;
    struct surface_state rendered;
    memset(&rendered,0,sizeof rendered);
    unsigned last_page = surface_hidden;
    bool has_frame = false;
    struct surface_state incoming;
    struct surface_animation animation = {0};
    uint64_t next_frame = 0;
    while (running && getppid() > 1) {
        fd_set available;
        struct timeval deadline = {1,0};
        if (next_frame) {
            uint64_t now = monotonic_ms();
            deadline.tv_sec=0;
            deadline.tv_usec=now < next_frame ? (next_frame-now)*1000u : 0;
        }
        FD_ZERO(&available);
        FD_SET(STDIN_FILENO,&available);
        int ready = select(STDIN_FILENO+1,&available,NULL,NULL,&deadline);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0 || (!ready && !next_frame)) {
            fprintf(stderr,"state channel %s (errno %d)\n",ready == 0 ? "timed out" : "select failed",errno);
            break;
        }
        uint64_t now = monotonic_ms();
        if (ready) {
            ssize_t count = read(STDIN_FILENO,&incoming,sizeof incoming);
            if (count<0 || !surface_packet_valid(&incoming,(size_t)count)) {
                if (count) fprintf(stderr,"State channel yielded %ld bytes (errno %d)\n",(long)count,errno);
                break;
            }
            if (incoming.page != last_page) {
                fprintf(stderr,"page %lu\n",(unsigned long)incoming.page);
                last_page=incoming.page;
            }
            if (incoming.page >= surface_hidden && !companion_surface_page(incoming.page)) {
                if (attached) {
                    layer_manager_hide();
                    attached = false;
                } else if (shown && screen_set_window_property_iv(window,SCREEN_PROPERTY_VISIBLE,&(int){0}) != 0) {
                    result=1;
                    break;
                }
                shown=0;
                has_frame=false;
                retries=0;
                next_frame=0;
                animation.page=surface_hidden;
                continue;
            }
            clean_state(&incoming);
            tach_animation_update(&animation,&incoming,now);
        }
        incoming.tach_visual = tach_animation_value(&animation,now);
        next_frame = incoming.tach_visual == animation.target ? 0 :
                     now + ((now-animation.since)%3u == 0 ? 16u : 17u);
        stage = "Screen post";
        if (!has_frame || memcmp(&rendered,&incoming,sizeof incoming)) {
            // The NBT Evo OMAP compositor presents these Screen buffer bytes in B,G,R,A order.
            struct canvas canvas = {pixels[index], (int)width, (int)height, strides[index], canvas_bgra};
            surface_draw(canvas,&incoming);
            if (screen_post_window(window,buffers[index],1,rect,0) != 0) { result=1; break; }
            index ^= 1;
            rendered=incoming;
            has_frame=true;
        }
        if (!shown && use_layer_manager) {
            stage = "LayerManager attach";
            enum layer_result layer = layer_manager_show((int)width,(int)height);
            if (layer == layer_error || (layer == layer_waiting && ++retries >= 16)) {
                result=1;
                break;
            }
            if (layer == layer_waiting) continue;
            fprintf(stderr,"LayerManager attached\n");
            attached = true;
        }
        retries = 1;
        if (!shown) {
            stage = "window visibility";
            shown=1;
            if (!use_layer_manager && screen_set_window_property_iv(window,SCREEN_PROPERTY_VISIBLE,&shown) != 0) { result=1; break; }
        }
    }

cleanup:
    if (result) fprintf(stderr,"Fullscreen failed during %s (errno %d)\n",stage,errno);
    if (window && shown && !use_layer_manager) {
        shown=0;
        screen_set_window_property_iv(window, SCREEN_PROPERTY_VISIBLE, &shown);
    }
    if (attached) layer_manager_hide();
    if (window) screen_destroy_window(window);
    if (context) screen_destroy_context(context);
    if (screen_library) dlclose(screen_library);
    return result;
}
