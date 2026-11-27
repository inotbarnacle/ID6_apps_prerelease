#define _POSIX_C_SOURCE 200809L
#include "n57.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned slot, bytes, clears, definitions, reads;
static int refuse_clear, wrong_index, pair, read_adjust, invalid_pair;
static unsigned relative_raw=0x8000;
static int model(void *ctx, unsigned ecu, const unsigned char *q, unsigned n,
                 const unsigned char *prefix, unsigned pn, unsigned char *r, unsigned cap,
                 unsigned timeout, unsigned *negative) {
    (void)ctx; (void)cap; (void)timeout;
    assert(ecu==0x12);
    memcpy(r,prefix,pn);
    if (n==3 && !memcmp(q,"\x22\xf1\x50",3)) {
        memcpy(r+3,wrong_index ? "\x04\x05\x06" : "\x01\x02\x03",3); return 6;
    }
    if (n==4 && !memcmp(q,"\x2c\x03\xf3\x03",4)) {
        clears++;
        if (refuse_clear) { *negative=(unsigned)refuse_clear; return -2; }
        slot=bytes=pair=0; return 4;
    }
    if (n==8 && !memcmp(q,"\x2c\x01\xf3\x03",4)) {
        assert(q[6]==1);
        slot=((unsigned)q[4]<<8)|q[5]; bytes+=q[7]; definitions++; return 4;
    }
    if (n==12 && !memcmp(q,"\x2c\x01\xf3\x03",4)) {
        assert(!memcmp(q+4,"\x59\x55\x01\x02\x4c\x14\x01\x02",8));
        pair=1;bytes+=4;definitions++;return 4;
    }
    if (n==3 && !memcmp(q,"\x22\xf3\x03",3)) {
        reads++;
        if (pair) {
            memcpy(r+3,invalid_pair ? "\xff\xff\x00\x01" : "\x06\x40\x00\x01",4);
            return (int)(3+bytes)+read_adjust;
        }
        if (slot==0x4746) { memcpy(r+3,"\x27\x10",2); return (int)(3+bytes); }
        if (slot==0x44bf) { memcpy(r+3,"\x00\x01\x86\xa0",4); return (int)(3+bytes); }
        if (slot==0x464c) {
            r[3]=(unsigned char)(relative_raw>>8); r[4]=(unsigned char)relative_raw;
            return (int)(3+bytes)+read_adjust;
        }
    }
    *negative=0x31; return -2;
}

int main(void) {
    double v;
    char path[]="/tmp/id6-n57-XXXXXX";
    int fd=mkstemp(path);
    FILE *file;
    assert(fd>=0 && write(fd,"010203\n",7)==7);
    close(fd);
    assert(n57_index_file(path)==0x010203);
    file=fopen(path,"w"); assert(file);
    fputs("010203 extra",file); fclose(file);
    assert(!n57_index_file(path));
    unlink(path);
    assert(!n57_index_file(path));
    assert(n57_parse_index("010203")==0x010203);
    assert(!n57_parse_index("000000") && !n57_parse_index("01020") && !n57_parse_index("010203x"));
    assert(!n57_parse_index("-00001") && !n57_parse_index("0x1234"));
    assert(n57_match(model,0,0x010203)==1);
    wrong_index=1; assert(n57_match(model,0,0x010203)==0); wrong_index=0;
    assert(n57_read(model,0,0x4746,&v)==1 && fabs(v-457.77)<1e-6);
    assert(n57_read(model,0,0x44bf,&v)==1 && v==100000);
    assert(clears==2 && definitions==2 && reads==2);
    /* Failed clear + APPEND: the exact-length check must refuse previous bytes. */
    refuse_clear=0x31;
    assert(n57_read(model,0,0x4746,&v)==0);
    refuse_clear=0x33;
    assert(n57_read(model,0,0x4746,&v)==0 && definitions==3);
    assert(!n57_decode(0x5955,(const unsigned char *)"\xff\xff",2,&v));
    assert(!n57_decode(0x4c14,(const unsigned char *)"\x00",1,&v));
    assert(!n57_decode(0x4746,(const unsigned char *)"\x00\x00\x00",3,&v));
    assert(n57_decode(0x4c14,(const unsigned char *)"\x00\x01",2,&v) && v>0 && v<1);
    assert(!n57_read(model,0,0x9999,&v));
    refuse_clear=0;
    {
        double rpm=-1,speed=-1;
        unsigned before=reads;
        assert(n57_read_drive(model,0,&rpm,&speed)==1 && rpm==800 && speed>0 && speed<1);
        assert(reads==before+1); /* Both DIDs use one definition and one four-byte F303 read. */
        for (read_adjust=-1;read_adjust<=1;read_adjust+=2) {
            rpm=speed=-1;
            assert(!n57_read_drive(model,0,&rpm,&speed) && rpm==-1 && speed==-1);
        }
        read_adjust=0;invalid_pair=1;rpm=speed=-1;
        assert(!n57_read_drive(model,0,&rpm,&speed) && rpm==-1 && speed==-1);
        invalid_pair=0;refuse_clear=0x31;
        assert(!n57_read_drive(model,0,&rpm,&speed)); /* A failed clear must not accept appended bytes. */
        refuse_clear=0x33;
        assert(!n57_read_drive(model,0,&rpm,&speed));
    }
    refuse_clear=0;
    {
        const unsigned char half[]={0x80,0}, asymmetric[]={1,0}, full[]={0xff,0xff};
        assert(n57_decode(0x464c,half,2,&v) && fabs(v-50.003968)<1e-9);
        assert(n57_decode(0x464c,asymmetric,2,&v) && fabs(v-.390656)<1e-9);
        assert(!n57_decode(0x464c,half,1,&v) && !n57_decode(0x464c,half,3,&v));
        assert(!n57_decode(0x464c,full,2,&v)); /* >100%, not a proven ECU sentinel. */
        assert(n57_read(model,0,0x464c,&v)==1 && fabs(v-50.003968)<1e-9);
        for (read_adjust=-1;read_adjust<=1;read_adjust+=2) {
            v=-1;
            assert(!n57_read(model,0,0x464c,&v) && v==-1);
        }
        read_adjust=0; relative_raw=0xffff; v=-1;
        assert(!n57_read(model,0,0x464c,&v) && v==-1);
        relative_raw=0; assert(n57_read(model,0,0x464c,&v)==1 && v==0);
        refuse_clear=0x31;
        assert(!n57_read(model,0,0x464c,&v)); /* An uncleared slot appends data. */
        refuse_clear=0x33;
        assert(!n57_read(model,0,0x464c,&v));
    }
    puts("N57: factory scales, relative torque, identity pin and clear/define/read boundaries OK");
    return 0;
}
