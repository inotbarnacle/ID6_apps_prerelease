#include "dpf_core.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

unsigned dpf_pid_mask(unsigned pid) {
    switch (pid) {
    case 0x7a: return 7u;
    case 0x7b: return 7u<<3;
    case 0x7c: return 15u<<6;
    case 0x05: return DPF_BIT(DPF_COOLANT);
    case 0x8b: return DPF_BIT(DPF_REGEN_PROGRESS)|DPF_BIT(DPF_REGEN_TYPE);
    default: return 0;
    }
}

static int value_valid(unsigned field,double value) {
    if (field>=DPF_COUNT || !isfinite(value)) return 0;
    if (field>=DPF_REGEN_PROGRESS) return value==0 || value==1;
    if (field==DPF_COOLANT) return value>=-40 && value<=215;
    if (field>=DPF_B1_INLET_TEMP) return value>=-40 && value<=DPF_TEMP_MAX_C;
    if (field==DPF_B1_DELTA || field==DPF_B2_DELTA) return value>=-327.68 && value<=327.67;
    return value>=0 && value<=655.35;
}

int dpf_decode(unsigned pid,const unsigned char *p,size_t size,struct dpf_sample *out) {
    struct dpf_sample sample;
    unsigned count,first,i;
    if (!out || !p || !dpf_pid_mask(pid)) return 0;
    memset(&sample,0,sizeof sample);
    if (pid==0x05) {
        if (size!=1) return 0;
        sample.known=sample.supported=sample.valid=DPF_BIT(DPF_COOLANT);
        sample.value[DPF_COOLANT]=(int)p[0]-40;
    } else if (pid==0x8b) {
        if (size!=7 || (p[0]&0x80) || (p[1]&0xf0)) return 0;
        sample.known=dpf_pid_mask(pid);
        sample.supported=sample.valid=(unsigned)(p[0]&3)<<DPF_REGEN_PROGRESS;
        for (i=0;i<2;++i)
            if (sample.valid & DPF_BIT(DPF_REGEN_PROGRESS+i))
                sample.value[DPF_REGEN_PROGRESS+i]=(p[1]>>i)&1;
    } else {
        const int temperature=pid==0x7c;
        count=temperature ? 4 : 3;
        first=temperature ? 6 : pid==0x7b ? 3 : 0;
        if (size!=1+2*count || (p[0] & (temperature ? 0xf0 : 0xf8))) return 0;
        sample.known=dpf_pid_mask(pid);
        sample.supported=(unsigned)p[0]<<first;
        for (i=0;i<count;++i) {
            const unsigned field=first+i, raw=((unsigned)p[1+2*i]<<8)|p[2+2*i];
            double value;
            if (!(sample.supported & DPF_BIT(field))) continue;
            if (temperature) value=((int)raw-400)/10.0;
            else if (i==0) value=(raw>=32768 ? (int)raw-65536 : (int)raw)/100.0;
            else value=raw/100.0;
            if (!value_valid(field,value)) continue;
            sample.valid|=DPF_BIT(field);
            sample.value[field]=value;
        }
    }
    *out=sample;
    return 1;
}

static int frame_valid(const struct dpf_frame *f) {
    unsigned i;
    if (!f || !f->seq || !f->worker_pid || f->worker_pid>INT_MAX || !f->token || !f->sent_ms ||
        (f->known & ~DPF_ALL) || (f->supported & ~f->known) || (f->valid & ~f->supported)) return 0;
    for (i=0;i<DPF_COUNT;++i)
        if ((f->valid & DPF_BIT(i)) && (!value_valid(i,f->value[i]) || f->age_ms[i]>999999)) return 0;
    return 1;
}

int dpf_encode(const struct dpf_frame *f,char *out,size_t size) {
    size_t used;
    unsigned i;
    int n;
    if (!out || !size || !frame_valid(f)) return -1;
    if (size>DPF_MAX_PACKET) size=DPF_MAX_PACKET;
    n=snprintf(out,size,"DPF2 %lu %lu %llu %llu %u %u %u",(unsigned long)f->seq,(unsigned long)f->worker_pid,
               (unsigned long long)f->token,(unsigned long long)f->sent_ms,f->known,f->supported,f->valid);
    if (n<0 || (size_t)n>=size) return -1;
    used=(size_t)n;
    for (i=0;i<DPF_COUNT;++i) {
        const int valid=(f->valid & DPF_BIT(i))!=0;
        const double value=valid ? f->value[i] : 0;
        const unsigned magnitude=(unsigned)((value<0 ? -value : value)*100+.5);
        /* The HMI process can use a non-C locale. Keep protocol punctuation
         * independent of LC_NUMERIC rather than using floating printf/strtod. */
        n=snprintf(out+used,size-used," %s%u.%02u %u",value<0 && magnitude ? "-" : "",
                   magnitude/100,magnitude%100,valid ? f->age_ms[i] : 0);
        if (n<0 || (size_t)n>=size-used) return -1;
        used+=(size_t)n;
    }
    if (used+1>=size) return -1;
    out[used++]='\n';out[used]=0;
    return (int)used;
}

static int unsigned_field(char **cursor,uint64_t maximum,uint64_t *out) {
    char *p=*cursor;
    uint64_t value=0;
    while (*p==' ') ++p;
    if (*p<'0' || *p>'9') return 0;
    do {
        const unsigned digit=(unsigned)(*p-'0');
        if (value>maximum/10 || (value==maximum/10 && digit>maximum%10)) return 0;
        value=value*10+digit;
        ++p;
    } while (*p>='0' && *p<='9');
    if (*p!=' ' && *p!='\n') return 0;
    *cursor=p;*out=value;
    return 1;
}

static int value_field(char **cursor,double *out) {
    char *p=*cursor,*start;
    unsigned integer=0,fraction=0,digits=0;
    int negative=0;
    while (*p==' ') ++p;
    start=p;
    if (*p=='-') { negative=1;++p; }
    if (*p<'0' || *p>'9') return 0;
    while (*p>='0' && *p<='9') {
        if (integer>100000) return 0;
        integer=integer*10+(unsigned)(*p-'0');
        ++p;
    }
    if (*p=='.') {
        ++p;
        if (*p<'0' || *p>'9') return 0;
        while (*p>='0' && *p<='9') {
            if (++digits>2) return 0;
            fraction=fraction*10+(unsigned)(*p-'0');
            ++p;
        }
    }
    if (*p!=' ' || p-start>24) return 0;
    if (digits==1) fraction*=10;
    *out=(negative ? -(double)(integer*100+fraction) : (double)(integer*100+fraction))/100;
    *cursor=p;
    return 1;
}

int dpf_parse(const char *data,size_t size,struct dpf_frame *out) {
    struct dpf_frame f;
    char packet[DPF_MAX_PACKET],*cursor;
    uint64_t integer;
    unsigned i,fields,mask;
    if (!data || !out || size<6 || size>=sizeof packet || data[size-1]!='\n' ||
        memchr(data,0,size) || memchr(data,'\n',size-1)) return 0;
    if (!memcmp(data,"DPF1 ",5)) fields=DPF_LEGACY_COUNT;
    else if (!memcmp(data,"DPF2 ",5)) fields=DPF_COUNT;
    else return 0;
    mask=DPF_BIT(fields)-1;
    memcpy(packet,data,size);packet[size]=0;
    memset(&f,0,sizeof f);cursor=packet+5;
    if (!unsigned_field(&cursor,UINT32_MAX,&integer)) return 0;
    f.seq=(uint32_t)integer;
    if (!unsigned_field(&cursor,INT_MAX,&integer)) return 0;
    f.worker_pid=(uint32_t)integer;
    if (!unsigned_field(&cursor,UINT64_MAX,&f.token) || !unsigned_field(&cursor,UINT64_MAX,&f.sent_ms)) return 0;
    if (!unsigned_field(&cursor,mask,&integer)) return 0;
    f.known=(unsigned)integer;
    if (!unsigned_field(&cursor,mask,&integer)) return 0;
    f.supported=(unsigned)integer;
    if (!unsigned_field(&cursor,mask,&integer)) return 0;
    f.valid=(unsigned)integer;
    for (i=0;i<fields;++i) {
        if (!value_field(&cursor,&f.value[i]) || !unsigned_field(&cursor,999999,&integer)) return 0;
        f.age_ms[i]=(unsigned)integer;
        if (!(f.valid & DPF_BIT(i)) && (f.value[i]!=0 || f.age_ms[i]!=0)) return 0;
    }
    if (*cursor!='\n' || cursor[1] || !frame_valid(&f)) return 0;
    *out=f;
    return 1;
}

int dpf_measurement_state(const struct dpf_frame *f,unsigned field,uint64_t elapsed) {
    const unsigned bit=field<DPF_COUNT ? DPF_BIT(field) : 0;
    if (!f || !bit || !(f->known & bit)) return DPF_UNKNOWN;
    if (!(f->supported & bit)) return DPF_UNSUPPORTED;
    if (!(f->valid & bit)) return DPF_UNAVAILABLE;
    if (elapsed>=DPF_STALE_MS || f->age_ms[field]>=DPF_STALE_MS-elapsed) return DPF_STALE;
    return DPF_VALID;
}

double dpf_display_value(unsigned field,double value,int metric) {
    if (field>=DPF_COUNT) return NAN;
    if (field>=DPF_REGEN_PROGRESS) return value;
    if (field>=DPF_B1_INLET_TEMP) return metric ? value : value*1.8+32;
    return metric ? value*10 : value*.145037738;
}

const char *dpf_unit(unsigned field,int metric) {
    if (field>=DPF_COUNT) return "";
    if (field>=DPF_REGEN_PROGRESS) return "";
    if (field>=DPF_B1_INLET_TEMP) return metric ? "C" : "F";
    return metric ? "mbar" : "psi";
}
