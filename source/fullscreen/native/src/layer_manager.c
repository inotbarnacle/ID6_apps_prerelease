#include <stdbool.h>
#include "layer_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef CONTROL
#define CONTROL "/opt/sys/bin/LayerManagerControl"
#endif
#define HMI_LAYER "0x80000000"

enum { max_surfaces = 16 };

bool layer_manager_available(void)
{
    return access(CONTROL,X_OK) == 0;
}

bool layer_manager_size(int *width, int *height)
{
    FILE *output = popen(CONTROL " get screen 0 2>/dev/null","r");
    if (!output) return false;
    bool found = false;
    char line[256];
    while (fgets(line,sizeof line,output)) {
        char *field = strstr(line,"resolution:");
        if (field && sscanf(field+strlen("resolution:")," x=%d, y=%d",width,height) == 2) {
            found = true;
            break;
        }
    }
    pclose(output);
    return found;
}

bool layer_manager_surface_bound(void)
{
    char command[128], line[256];
    snprintf(command,sizeof command,CONTROL " get surface %u 2>/dev/null",LIBRARY_SURFACE);
    FILE *output = popen(command,"r");
    if (!output) return false;
    bool bound = false;
    while (fgets(line,sizeof line,output)) {
        const char *field = strstr(line,"native surface:");
        if (!field) continue;
        unsigned long long id = strtoull(field+strlen("native surface:"),NULL,10);
        bound = id && id < 4294967295ULL;
    }
    pclose(output);
    return bound;
}

static int read_order(unsigned *ids)
{
    FILE *output = popen(CONTROL " get layer " HMI_LAYER " 2>/dev/null","r");
    if (!output) return -1;
    int count = -1;
    char line[512];
    const char label[] = "surface render order:";
    while (fgets(line,sizeof line,output)) {
        char *pos = strstr(line,label);
        if (!pos) continue;
        if (!strchr(pos,'\n')) break;
        pos += sizeof label - 1;
        count = 0;
        while (*pos && *pos != '\n') {
            pos += strspn(pos," ,\t\r");
            if (!*pos || *pos == '\n') break;
            char *end;
            unsigned long id = strtoul(pos,&end,0);
            if (pos == end || id > 65535 || count == max_surfaces) { count = -1; break; }
            ids[count++] = (unsigned)id;
            pos = end;
            if (*pos == '(') {
                pos = strchr(pos,')');
                if (!pos) { count = -1; break; }
                ++pos;
            }
        }
        break;
    }
    pclose(output);
    return count;
}

static bool set_order(const unsigned *ids, int count)
{
    if (count <= 0 || count > max_surfaces) return false;
    char command[512];
    int used = snprintf(command,sizeof command,CONTROL " set layer " HMI_LAYER " render order ");
    for (int i = 0; i < count; ++i)
        used += snprintf(command+used,sizeof command-used,"%s0x%x",i ? "," : "",ids[i]);
    snprintf(command+used,sizeof command-used," >/dev/null 2>&1");
    return system(command) == 0;
}

static bool own_visibility(unsigned visible)
{
    char command[144];
    snprintf(command,sizeof command,CONTROL " set surface %u visibility %u >/dev/null 2>&1",
             LIBRARY_SURFACE,visible);
    return system(command) == 0;
}

enum layer_result layer_manager_show(int width, int height)
{
    if (!layer_manager_surface_bound()) return layer_waiting;

    unsigned ids[max_surfaces];
    int count = read_order(ids);
    if (count < 0) return layer_error;
    if (count != 1 || ids[0] != FACTORY_HMI_SURFACE) return layer_waiting;

    char command[512];
    snprintf(command,sizeof command,
             CONTROL " set surface %u source region 0 0 %d %d >/dev/null 2>&1 && "
             CONTROL " set surface %u destination region 0 0 %d %d >/dev/null 2>&1 && "
             CONTROL " set surface %u opacity 1.0 >/dev/null 2>&1 && "
             CONTROL " set surface %u accept 0 input events from devices 7 >/dev/null 2>&1",
             LIBRARY_SURFACE,width,height,LIBRARY_SURFACE,width,height,LIBRARY_SURFACE,LIBRARY_SURFACE);
    if (system(command) != 0) return layer_error;
    count = read_order(ids);
    if (count != 1 || ids[0] != FACTORY_HMI_SURFACE) return layer_waiting;
    ids[1] = ids[0];
    ids[0] = LIBRARY_SURFACE;
    if (!set_order(ids,2) || !own_visibility(1)) {
        layer_manager_hide();
        return layer_error;
    }
    return layer_ready;
}

void layer_manager_hide(void)
{
    own_visibility(0);
    unsigned ids[max_surfaces];
    int count = read_order(ids), remaining = 0;
    for (int i = 0; i < count; ++i)
        if (ids[i] != LIBRARY_SURFACE) ids[remaining++] = ids[i];
    if (remaining > 0 && remaining < count) set_order(ids,remaining);
}
