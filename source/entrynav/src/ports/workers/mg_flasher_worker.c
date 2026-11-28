#define _POSIX_C_SOURCE 200809L
#include "diag_link.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { MAP, BURBLE, COOLING, ANTILAG, EXHAUST, VALET, ETHANOL_ENABLE,
       ETHANOL_VALUE, RADIATOR, ETHANOL_CONTENT, ANTILAG_COUNT, VALET_COUNT,
       CONFIRM_BYTE, FEATURE_COUNT };
struct feature {
    const char *option, *label;
    unsigned did, address[2], size, min, max, read_multiplier;
};
static const struct feature features[] = {
    {"--map", "Map slot", 0x6945,{0x5180F606,0x60107F32},2,0,4,1},
    {"--burbles", "Burbles", 0x6946,{0x5180F66A,0x60107F67},1,0,5,1},
    {"--cooling", "Max Cooling", 0x6947,{0x5180F6A1,0x60107F98},1,0,1,1},
    {"--antilag", "Antilag RPM", 0x6948,{0x5180F5DA,0x60107F1A},2,15,60,200},
    {"--exhaust", "Exhaust flap", 0x6949,{0x5180F675,0x60107F6F},1,0,2,1},
    {"--valet", "Valet mode", 0x694A,{0x5180F6F5,0x60107FC7},1,0,1,1},
    {"--ethanol-enable", "Ethanol override", 0x694B,{0x5180F691,0x60107F8B},1,0,1,1},
    {"--ethanol-value", "Ethanol override value", 0x694C,{0x5180F5FC,0x60107F26},2,0,100,200},
    {"--radiator", "Radiator flap", 0x694E,{0,0xB0007345},1,0,2,1},
    {"--ethanol-content", "Ethanol content", 0,{0x5180F5FE,0x60107F28},2,0,100,1},
    {"--antilag-count", "Antilag counter", 0,{0x5180F5D8,0x60107F18},2,0,65535,1},
    {"--valet-count", "Valet limits exceeded", 0,{0x5180F6F6,0x60107FC8},1,0,255,1},
    {"--confirm-byte", "Confirmation byte", 0,{0x5180F667,0x60107F64},1,0,255,1},
};
static const unsigned active_did[]={0x5180F5D4,0x60107F14};

static int load_generation(const char *path) {
    FILE *file=fopen(path,"r");
    if (!file) return -1;
    char line[32];
    const int valid=fgets(line,sizeof line,file)!=NULL && fgetc(file)==EOF && !ferror(file);
    fclose(file);
    if (!valid) return -1;
    if (!strcmp(line,"MGF73 GEN1\n")) return 0;
    if (!strcmp(line,"MGF73 GEN2\n")) return 1;
    return -1;
}

static int read_value(struct diag_link *link,int generation,unsigned address,
                      unsigned width,unsigned *value) {
    unsigned char request[]={0x23,0x14,(unsigned char)(address>>24),(unsigned char)(address>>16),
                           (unsigned char)(address>>8),(unsigned char)address,(unsigned char)width};
    const unsigned char positive[]={0x63};
    unsigned char reply[3];
    unsigned negative;
    if (diag_link_request(link,0x12,request,sizeof request,positive,sizeof positive,
                          reply,sizeof reply,750,&negative)!=(int)width+1) return 0;
    *value=width==1 ? reply[1] :
           generation==0 ? ((unsigned)reply[1]<<8)|reply[2] : ((unsigned)reply[2]<<8)|reply[1];
    return 1;
}

static int valid_reading(unsigned index,unsigned raw) {
    const struct feature *f=&features[index];
    if (index==ANTILAG) return !(raw%2) && raw>=f->min*f->read_multiplier && raw<=f->max*f->read_multiplier;
    return raw>=f->min*f->read_multiplier && raw<=f->max*f->read_multiplier;
}

static void describe(unsigned index,unsigned raw) {
    printf("%s: ",features[index].label);
    const char *const burbles[]={"OEM","App Slider","Level 1","Level 2","Level 3","Off"};
    const char *const flap[]={"OEM","Closed","Open"};
    switch (index) {
    case BURBLE: puts(burbles[raw]); break;
    case COOLING: case VALET: case ETHANOL_ENABLE: puts(raw ? "On" : "Off"); break;
    case EXHAUST: case RADIATOR: puts(flap[raw]); break;
    case ANTILAG: printf("%u RPM\n",raw/2); break;
    case ETHANOL_VALUE: printf("%.2f%%\n",raw/200.0); break;
    case ETHANOL_CONTENT: printf("%u%%\n",raw); break;
    case CONFIRM_BYTE: printf("0x%02X\n",raw); break;
    default: printf("%u\n",raw); break;
    }
}

static void show_setting(struct diag_link *link,int generation,unsigned index) {
    const struct feature *f=&features[index];
    unsigned raw;
    if (!f->address[generation]) return;
    if (read_value(link,generation,f->address[generation],f->size,&raw) && valid_reading(index,raw))
        describe(index,raw);
    else printf("%s: unavailable\n",f->label);
}

static void control(struct diag_link *link,int generation,unsigned index,unsigned value) {
    const struct feature *f=&features[index];
    unsigned raw, echoed, negative;
    if (!read_value(link,generation,f->address[generation],f->size,&raw) || !valid_reading(index,raw)) {
        puts("Cannot read a valid current setting. No command sent.");
        return;
    }
    const unsigned high=f->did>>8, low=f->did&255;
    const unsigned char request[]={0x2e,(unsigned char)high,(unsigned char)low,(unsigned char)value,
                                   (unsigned char)(high+low+value)};
    const unsigned char positive[]={0x6e,(unsigned char)high,(unsigned char)low};
    unsigned char reply[8];
    for (unsigned retry=0; ; ++retry) {
        const int result=diag_link_request(link,0x12,request,sizeof request,positive,sizeof positive,
                                           reply,sizeof reply,1600,&negative);
        if (result==-2 && negative==0x21) {
            if (retry==20) { puts("DME stayed busy. Setting unconfirmed."); return; }
            struct timespec delay={0,200000000L};
            while (nanosleep(&delay,&delay)<0 && errno==EINTR && diag_running) {}
            if (!diag_running) return;
            continue;
        }
        if (!(result==-2 && negative==0x69) && result!=3) {
            if (result==-2) printf("MG Flasher request refused (NRC %02X).\n",negative);
            else puts("No matching DME acknowledgement. Setting unconfirmed.");
            return;
        }
        break;
    }
    if (!read_value(link,generation,active_did[generation],2,&echoed)) {
        puts("Acknowledged by DME; active DID unconfirmed.");
    } else if (echoed==0x9696) {
        puts("DME rejected the checksum.");
    } else if (echoed==0x6969) {
        puts("DME rejected the identifier or value.");
    } else if (echoed==f->did && read_value(link,generation,f->address[generation],f->size,&raw)
               && raw==value*f->read_multiplier) {
        fputs("Verified: ",stdout);
        describe(index,raw);
    } else {
        puts("Acknowledged by DME; setting did not verify.");
    }
}

static int parse_number(const char *text,unsigned max,unsigned *value) {
    if (!*text || strspn(text,"0123456789")!=strlen(text)) return 0;
    unsigned long n=strtoul(text,NULL,10);
    if (n>max) return 0;
    *value=(unsigned)n;
    return 1;
}

int main(int argc,char **argv) {
    if ((argc!=7 && argc!=9) || strcmp(argv[1],"--host") || strcmp(argv[3],"--profile")) return 2;
    const int status=!strcmp(argv[5],"--status");
    unsigned index=0,value;
    while (index<FEATURE_COUNT && strcmp(argv[5],features[index].option)) ++index;
    if ((!status && index==FEATURE_COUNT) || !parse_number(argv[6],100,&value)) return 2;
    const int writing=!status && features[index].did;
    if (writing ? value<features[index].min || value>features[index].max : value!=0) return 2;
    struct diag_link_cfg cfg={argv[2],6801,DIAG_ONESHOT};
    if (argc==9 && (strcmp(argv[7],"--port") || !parse_number(argv[8],65535,&cfg.port) || !cfg.port)) return 2;

    const int generation=load_generation(argv[4]);
    if (generation<0) {
        puts("Select the DME generation for MG Custom Code 7.3–7.3.4 first.");
        return 0;
    }
    if (!status && !features[index].address[generation]) {
        puts("This control is available on Gen 2 only.");
        return 0;
    }
    struct diag_link link;
    diag_link_signals();
    diag_link_init(&link,&cfg);
    if (!diag_link_reserve_oneshot(&link)) puts("Diagnostics busy. Try again.");
    else if (!diag_link_up(&link)) puts("HSFZ diagnostic gateway unavailable.");
    else if (writing) control(&link,generation,index,value);
    else if (status) {
        for (unsigned i=0;i<FEATURE_COUNT && diag_running;++i) show_setting(&link,generation,i);
    } else show_setting(&link,generation,index);
    diag_link_close(&link);
    return 0;
}
