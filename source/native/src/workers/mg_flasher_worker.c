#define _POSIX_C_SOURCE 200809L
#include "diag_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAP, BURBLE, COOLING, FEATURE_COUNT };
static const unsigned limits[]={4,5,1};
static const unsigned dids[]={0x6945,0x6946,0x6947};
static const char *const names[]={"Map slot", "Burble slot", "Max Cooling"};

struct locator {
    unsigned active, width, echo;
};

struct profile {
    unsigned size_bytes, big_endian;
    struct locator feature[FEATURE_COUNT];
};

/* MGF1 files include the old identity and slot masks. Only the addressing
 * fields are used for readback. The MGF2 header and rows omit those extras. */
static int load_profile(const char *path, struct profile *p) {
    char line[128], version[8], endian[4];
    FILE *file=fopen(path,"r");
    if (!file) return 0;
    int valid=fgets(line,sizeof line,file)!=0 &&
        sscanf(line,"%7s %u %3s",version,&p->size_bytes,endian)==3 &&
        (!strcmp(version,"MGF1") || !strcmp(version,"MGF2")) &&
        (p->size_bytes==1 || p->size_bytes==2) && (!strcmp(endian,"LE") || !strcmp(endian,"BE"));
    if (valid) p->big_endian=!strcmp(endian,"BE");
    unsigned configured=0;
    for (unsigned i=0;valid && i<FEATURE_COUNT;++i) {
        struct locator *l=&p->feature[i];
        valid=fgets(line,sizeof line,file)!=0 &&
            sscanf(line,"%x %u %x",&l->active,&l->width,&l->echo)==3 &&
            (!l->active || (l->echo && (l->width==1 || l->width==2 || l->width==4)));
        configured |= l->active;
    }
    fclose(file);
    return valid && configured;
}

/* The selected profile supplies ROM-specific, read-only locator fields. */
static int read_memory(struct diag_link *link, const struct profile *p, unsigned address,
                       unsigned width, unsigned char *out, unsigned *negative) {
    unsigned char request[]={0x23, (unsigned char)(p->size_bytes*16+4),
        (unsigned char)(address>>24),(unsigned char)(address>>16),(unsigned char)(address>>8),
        (unsigned char)address,0,(unsigned char)width};
    if (p->size_bytes==1) request[6]=(unsigned char)width;
    static const unsigned char positive[]={0x63};
    unsigned char reply[33];
    const int count=diag_link_request(link,0x12,request,6+p->size_bytes,
        positive,sizeof positive,reply,sizeof reply,750,negative);
    if (count!=(int)width+1) return count<0 ? count : 0;
    memcpy(out,reply+1,width);
    return 1;
}

static int read_value(struct diag_link *link, const struct profile *p, unsigned addr,
                      unsigned width, unsigned *value, unsigned *negative) {
    unsigned char data[4];
    const int rc=read_memory(link,p,addr,width,data,negative);
    if (rc!=1) return rc;
    *value=0;
    for (unsigned i=0;i<width;++i)
        *value=(*value<<8)|data[p->big_endian ? i : width-1-i];
    return 1;
}

static void describe(unsigned feature, int value) {
    if (value<0) printf("%s unavailable",names[feature]);
    else if (feature==COOLING) printf("Max Cooling %s",value ? "On" : "Off");
    else printf("%s %d",names[feature],value);
}

static int current(struct diag_link *link, const struct profile *p, unsigned feature) {
    const struct locator *l=&p->feature[feature];
    unsigned value, negative;
    const int rc=l->active ? read_value(link,p,l->active,l->width,&value,&negative) : 0;
    if (rc!=1 || value>limits[feature]) return rc==-1 ? -2 : -1;
    return (int)value;
}

static void control(struct diag_link *link, const struct profile *p, unsigned feature, unsigned value) {
    unsigned negative, echoed;
    unsigned char reply[8];
    if (feature==FEATURE_COUNT) {
        for (unsigned i=0;i<FEATURE_COUNT;++i) {
            if (i) fputs("  |  ",stdout);
            const int value=current(link,p,i);
            describe(i,value);
            if (value==-2) break;
        }
        putchar('\n');
        return;
    }
    const unsigned did=dids[feature];
    const unsigned char hi=(unsigned char)(did>>8), lo=(unsigned char)did;
    const unsigned char request[]={0x2e,hi,lo,(unsigned char)value,(unsigned char)(hi+lo+value)};
    const unsigned char positive[]={0x6e,hi,lo};
    const int accepted=diag_link_request(link,0x12,request,sizeof request,
        positive,sizeof positive,reply,sizeof reply,1600,&negative);
    if (accepted==-2) {
        printf("MG Flasher request refused (NRC %02X).\n",negative);
        return;
    }
    if (accepted!=3) {
        puts("No matching DME acknowledgement.");
        return;
    }
    if (p && p->feature[feature].active) {
        const struct locator *l=&p->feature[feature];
        if (read_value(link,p,l->echo,2,&echoed,&negative)==1 &&
            echoed==did && current(link,p,feature)==(int)value) {
            fputs("Verified: ",stdout);
            describe(feature,(int)value);
            putchar('\n');
            return;
        }
        puts("Acknowledged by DME; active setting unconfirmed.");
        return;
    }
    fputs("Acknowledged by DME: ",stdout);
    describe(feature,(int)value);
    putchar('\n');
}

int main(int argc,char **argv) {
    if ((argc!=7 && argc!=9) || strcmp(argv[1],"--host") || strcmp(argv[3],"--profile")) return 2;
    unsigned feature=FEATURE_COUNT;
    if (!strcmp(argv[5],"--map")) feature=MAP;
    else if (!strcmp(argv[5],"--burbles")) feature=BURBLE;
    else if (!strcmp(argv[5],"--cooling")) feature=COOLING;
    else if (strcmp(argv[5],"--status")) return 2;
    if (argv[6][0]<'0' || argv[6][0]>'9' || argv[6][1]) return 2;
    const unsigned value=(unsigned)(argv[6][0]-'0');
    if (value>(feature==FEATURE_COUNT ? 0 : limits[feature])) return 2;
    struct diag_link_cfg cfg={argv[2],6801,DIAG_AUTO,DIAG_ONESHOT,"mgflasher",0};
    if (argc==9) {
        char *end;
        const unsigned long port=strtoul(argv[8],&end,10);
        if (strcmp(argv[7],"--port") || *end || !port || port>65535) return 2;
        cfg.port=(unsigned)port;
    }
    struct profile profile;
    memset(&profile,0,sizeof profile);
    const struct profile *locators=load_profile(argv[4],&profile) ? &profile : 0;
    if (feature==FEATURE_COUNT && !locators) {
        puts("Active settings readback needs vehicle locators. Live commands are available.");
        return 0;
    }
    struct diag_link link;
    diag_link_signals();
    diag_link_init(&link,&cfg);
    if (!diag_link_reserve_oneshot(&link)) puts("Diagnostics busy. Try again.");
    else if (!diag_link_up(&link)) puts("HSFZ diagnostic gateway unavailable.");
    else control(&link,locators,feature,value);
    diag_link_close(&link);
    return 0;
}
