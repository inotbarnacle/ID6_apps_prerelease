#ifndef ID6_DCT_TEMPERATURE_H
#define ID6_DCT_TEMPERATURE_H

#include <stdio.h>
#include "diag_link.h"

#define ID6_DCT_PREFERENCE "/var/opt/hmi/data/ID5/ID6DctTemperature.prefs"

static inline int dct_temperature_enabled(void) {
#ifdef __QNX__
    FILE *file=fopen(ID6_DCT_PREFERENCE,"rb");
    if (!file) return 0;
    const int enabled=fgetc(file)=='1' && fgetc(file)==EOF && !ferror(file);
    fclose(file);
    return enabled;
#else
    return 0;
#endif
}

/* GS7D36SG captures in issue #177 document signed oil temperatures in
 * the 38-byte DE31 block. This selection does not affect Park detection. */
static inline int read_dct_temperature(struct diag_link *link,double *value) {
    static const unsigned char status[]={0x22,0xde,0x31}, status_ok[]={0x62,0xde,0x31};
    unsigned char reply[64];
    unsigned negative;
    int size=diag_link_request(link,0x18,status,sizeof status,status_ok,sizeof status_ok,
        reply,sizeof reply,500,&negative);
    if (size>=0) {
        if (size!=3+38) return 0;
        int raw=((int)reply[23]<<8)|reply[24];
        if (raw>=32768) raw-=65536;
        if (raw < -48 || raw > 180) return 0;
        *value=raw;
        return 1;
    }
    if (size!=-2) return size;
    if (negative!=0x11 && negative!=0x12 && negative!=0x31) return 0;

    /* Earlier gearbox profiles use the legacy local identifier. */
    static const unsigned char request[]={0x21,0x0a}, prefix[]={0x61,0x0a};
    size=diag_link_request(link,0x18,request,sizeof request,prefix,sizeof prefix,
        reply,sizeof reply,500,&negative);
    if (size<0) return size==-2 ? 0 : size;
    if (size!=3 || reply[2]<7 || reply[2]>235) return 0;
    *value=(int)reply[2]-55;
    return 1;
}

#endif
