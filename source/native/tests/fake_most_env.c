/* Host workers only: decides what the fake DSI does when a worker falls back to MOST.
 * ID6_FAKE_MOST unset: no library, as on a unit without DSI. "up": a DME answering the gauge DIDs, OBD
 * 01 0C / 01 0D, the MHD slot selector, and the fault code read and clear. "no-server": the library loads
 * but no server answers. "drop": the link is lost at the fifth read. "mhd-refuse": "up", but 2F gets
 * 7F 2F 31. "codes-512" and "codes-513": "up", but 19 02 returns 127 codes in 512 or 513 bytes.
 * ID6_FAKE_MOST_DELAY_MS (0-2000) slows each reply and the close by that much.
 * ID6_FAKE_MOST_READ_DELAY_MS (0-5000) additionally delays the F303 data reply only.
 * Each bind prints a marker the tests look for. */
/* n57-up and n57-{wrong-pin,bad-pin,refuse-clear,refuse-define,short-read,invalid-rpm,drop}
 * use a stateful synthetic DDE; FAKE-N57 lines capture each actual inner request and reply. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "most/dsi_abi.h"
#include "fake_dme.h"

int fake_most_binds;   /* most_fake_bind calls in this process */

static void report(void)
{
    int i;
    for (i=0;i<fake_nreq;i++) if (fake_req[i].id==0x0f && fake_req[i].n>0 &&
                                 fake_req[i].w[0].align==0)
        fprintf(stderr,"FAKE-MOST client=%.*s\n",fake_req[i].w[0].size,
                (const char *)fake_req[i].w[0].bytes);
    fprintf(stderr, "FAKE-MOST exit unregister_target=%d unregister_client=%d free=%d\n",
            fake_dsi_req_count(0x05), fake_dsi_req_count(0x10), fake_free_calls);
}

/* 59 02, the status mask, 127 codes of four bytes, then padding to n bytes. */
static void long_codes(size_t n)
{
    uint8_t b[513];
    size_t i;
    memset(b, 0, sizeof b);
    b[0] = 0x59; b[1] = 0x02; b[2] = 0xFF;
    for (i = 0; i < 127; ++i) {
        b[3 + 4 * i] = 0x01; b[4 + 4 * i] = (uint8_t)(i >> 8); b[5 + 4 * i] = (uint8_t)i; b[6 + 4 * i] = 0x08;
    }
    fake_dme_raw("1902", b, n);
}

/* Synthetic identity only: 010203 is not a claim of DDE compatibility. Each F303 definition is
 * validated against a clear and the exact catalog width before it may produce a measurement. */
static const struct { unsigned did, width; uint32_t raw; } n57_values[] = {
    {0x5955,2,1600}, {0x4c14,2,0}, {0x4841,2,25122}, {0x4cf0,2,32768},
    {0x4746,2,39321}, {0x461b,2,19000}, {0x4846,2,2931}, {0x4517,2,19000},
    {0x5242,2,14500}, {0x47dc,2,1000}, {0x50a6,2,2931}, {0x44be,2,1000},
    {0x44f8,2,22000}, {0x44bf,4,1500000}, {0x44ef,2,16000}, {0x44f2,2,18000},
    {0x5306,2,50000}, {0x48a5,2,28000}, {0x4458,2,26000}, {0x464c,2,32768}
};
static const char *n57_mode;
static int n57_slots[2], n57_slot_n, n57_cleared, n57_reads;

static int n57_reply(const uint8_t *q, size_t n, uint8_t *out, size_t *out_n)
{
    unsigned i, did;
    char line[160];
    size_t used;
    *out_n=3;out[0]=0x7f;out[1]=q[0];out[2]=0x31;
    if (n==3 && !memcmp(q,"\x22\xf1\x50",3)) {
        memcpy(out,"\x62\xf1\x50\x01\x02\x03",6);*out_n=6;
        if (!strcmp(n57_mode,"n57-wrong-pin")) out[5]=4;
        if (!strcmp(n57_mode,"n57-bad-pin")) *out_n=5;
    } else if (n==4 && !memcmp(q,"\x2c\x03\xf3\x03",4)) {
        n57_slot_n=0;n57_cleared=1;
        if (!strcmp(n57_mode,"n57-refuse-clear")) out[2]=0x33;
        else {memcpy(out,"\x6c\x03\xf3\x03",4);*out_n=4;}
    } else if ((n==8 || n==12) && !memcmp(q,"\x2c\x01\xf3\x03",4)) {
        unsigned entry, count=(unsigned)(n-4)/4;
        int slots[2];
        for (entry=0;entry<count;entry++) {
            const uint8_t *definition=q+4+4*entry;
            did=((unsigned)definition[0]<<8)|definition[1];
            for(i=0;i<sizeof n57_values/sizeof n57_values[0];i++)
                if(n57_values[i].did==did)break;
            if (i==sizeof n57_values/sizeof n57_values[0] || definition[2]!=1 ||
                definition[3]!=n57_values[i].width) break;
            slots[entry]=(int)i;
        }
        if (!n57_cleared || n57_slot_n) out[2]=0x24;
        else if (!strcmp(n57_mode,"n57-refuse-define")) out[2]=0x31;
        else if (entry==count) {
            memcpy(n57_slots,slots,count*sizeof slots[0]);n57_slot_n=(int)count;
            memcpy(out,"\x6c\x01\xf3\x03",4);*out_n=4;
        }
    } else if (n==3 && !memcmp(q,"\x22\xf3\x03",3) && n57_slot_n) {
        ++n57_reads;
        if (strcmp(n57_mode,"n57-refuse-read-once") || n57_reads!=2) {
            int entry;
            memcpy(out,"\x62\xf3\x03",3);
            for (entry=0;entry<n57_slot_n;entry++) {
                int slot=n57_slots[entry];
                uint32_t raw=n57_values[slot].raw;
                unsigned width=n57_values[slot].width;
                if (!strcmp(n57_mode,"n57-invalid-rpm") && n57_values[slot].did==0x5955) raw=65535;
                for(i=0;i<width;i++)out[*out_n+i]=(uint8_t)(raw>>(8*(width-1-i)));
                *out_n+=width;
            }
            if (!strcmp(n57_mode,"n57-short-read")) --*out_n;
        }
    }
    used=(size_t)snprintf(line,sizeof line,"FAKE-N57 q=");
    for(i=0;i<n;i++)used+=(size_t)snprintf(line+used,sizeof line-used,"%02X",q[i]);
    used+=(size_t)snprintf(line+used,sizeof line-used," r=");
    for(i=0;i<*out_n;i++)used+=(size_t)snprintf(line+used,sizeof line-used,"%02X",out[i]);
    snprintf(line+used,sizeof line-used,"\n");fputs(line,stderr);fflush(stderr);
    return 1;
}

int most_fake_bind(void)
{
    static const struct { uint16_t did; uint8_t n, b[2]; } dids[] = {
        {0x4201,2,{0x63,0xca}}, {0x4205,2,{0x31,0xe5}}, {0x5889,2,{0x10,0x4e}}, {0x4a49,2,{0x00,0x4e}},
        {0x580f,1,{0x82,0}}, {0x56d7,2,{0x11,0x69}}, {0x586f,2,{0x09,0xc1}}, {0x4300,2,{0x0b,0xf8}}
    };
    static const uint8_t rpm[] = {0x41,0x0C,0x1A,0xF8}, kph[] = {0x41,0x0D,0x3C}, cleared[] = {0x54};
    static const uint8_t unmatched[] = {0x62,0x00,0x00};
    static const uint8_t codes[] = {0x59,0x02,0xFF,0x12,0x34,0x56,0x2F,0x0A,0xBC,0xDE,0x08};
    const char *mode = getenv("ID6_FAKE_MOST"), *delay = getenv("ID6_FAKE_MOST_DELAY_MS");
    const char *read_delay = getenv("ID6_FAKE_MOST_READ_DELAY_MS");
    unsigned i;
    ++fake_most_binds;
    fprintf(stderr, "FAKE-MOST bind %s\n", mode ? mode : "unset");
    if (!mode) return -1;
    fake_dsi_reset();
    atexit(report);
    if (!strcmp(mode, "no-server")) { fake_dme_start(FAKE_NO_SERVER); return 0; }
    if (strcmp(mode, "up") && strcmp(mode, "drop") && strcmp(mode, "mhd-refuse") &&
        strcmp(mode, "codes-512") && strcmp(mode, "codes-513") &&
        strcmp(mode, "bm3-unknown") && strcmp(mode, "gauge-mismatch") &&
        strcmp(mode,"n57-up") && strcmp(mode,"n57-wrong-pin") &&
        strcmp(mode,"n57-bad-pin") && strcmp(mode,"n57-refuse-clear") &&
        strcmp(mode,"n57-refuse-define") && strcmp(mode,"n57-short-read") &&
        strcmp(mode,"n57-invalid-rpm") && strcmp(mode,"n57-refuse-read-once") && strcmp(mode,"n57-drop")) return -1;
    fake_dme_start(FAKE_UP);
    if (delay && atoi(delay) > 0 && atoi(delay) <= 2000) fake_dme_hold_ms(atoi(delay));
    if (read_delay && atoi(read_delay)>0 && atoi(read_delay)<=5000) fake_dme_hold_read_ms(atoi(read_delay));
    if (!strncmp(mode,"n57-",4)) {
        n57_mode=mode;n57_slot_n=n57_cleared=n57_reads=0;
        fake_dme_set_reply_hook(n57_reply);
        if (!strcmp(mode,"n57-drop")) fake_dme_drop_link_after(5);
        return 0;
    }
    if (!strcmp(mode, "drop")) fake_dme_drop_link_after(5);
    if (!strcmp(mode, "mhd-refuse")) fake_dme_refuse("2F", 0x31, 0);
    if (!strcmp(mode, "codes-512")) long_codes(512);
    if (!strcmp(mode, "codes-513")) long_codes(513);
    for (i = 0; i < sizeof dids / sizeof dids[0]; ++i) {
        if (!strcmp(mode, "gauge-mismatch") && dids[i].did == 0x5889)
            fake_dme_raw("225889", unmatched, sizeof unmatched);
        else
            fake_dme_plain(dids[i].did, dids[i].b, dids[i].n);
    }
    fake_dme_raw("010C", rpm, sizeof rpm);
    fake_dme_raw("010D", kph, sizeof kph);
    for (i = 0; i < 4; ++i) {   /* 2F 01 <slot> echoes the slot */
        char hex[7];
        uint8_t echo[3] = {0x6F, 0x01, 0};
        echo[2] = (uint8_t)i;
        snprintf(hex, sizeof hex, "2F01%02X", i);
        fake_dme_raw(hex, echo, sizeof echo);
    }
    {
        static const uint8_t clear[] = {0x6c, 0x03, 0xf3, 0x01}, define[] = {0x6c, 0x02, 0xf3, 0x01};
        static const uint8_t map[] = {0x7d}, map_read[] = {0x63, 1};
        static const uint8_t sound[] = {0x6e, 0x5f, 0xfd};
        static const uint8_t flap_auto[] = {0x6f, 0x60, 0xfd, 0};
        static const uint8_t flap_adjust[] = {0x6f, 0x60, 0xfd, 3};
        uint8_t rom[258] = {0x62, 0xf3, 0x01};
        const char *profile = !strcmp(mode, "bm3-unknown") ? "BM3_R #UNKNOWN" : "BM3_R #011000";
        memcpy(rom + 3, profile, strlen(profile));
        fake_dme_raw("2C03F301", clear, sizeof clear);
        fake_dme_raw("2C02F30124807FFE5000FF", define, sizeof define);
        fake_dme_raw("22F301", rom, sizeof rom);
        fake_dme_raw("3D14D001D006020101", map, sizeof map);
        fake_dme_raw("2314D001D00601", map_read, sizeof map_read);
        fake_dme_raw("2E5FFD00", sound, sizeof sound);
        fake_dme_raw("2F60FD00", flap_auto, sizeof flap_auto);
        fake_dme_raw("2F60FD0314FE", flap_adjust, sizeof flap_adjust);
        fake_dme_raw("2F60FD032328", flap_adjust, sizeof flap_adjust);
    }
    fake_dme_raw("1902AF", codes, sizeof codes);
    fake_dme_raw("14FFFFFF", cleared, sizeof cleared);
    return 0;
}
