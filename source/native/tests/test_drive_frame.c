#include "drive_frame.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    struct drive_frame legacy={123,800,100};
    struct drive_n57_frame native={DRIVE_N57_MAGIC,234,4500,5};
    struct drive_sample out;
    assert(sizeof legacy==16 && sizeof native==24);
    assert(drive_parse(&legacy,sizeof legacy,&out) && out.mkph==100000 && !out.n57);
    assert(drive_parse(&native,sizeof native,&out) && out.mkph==5 && out.n57 && out.rpm==4500);
    { struct drive_frame old_receiver;
      memcpy(&old_receiver,&native,sizeof old_receiver);
      assert(old_receiver.sampled_ms==DRIVE_N57_MAGIC && old_receiver.sampled_ms>UINT64_C(1000000000000)); }
    assert(drive_display_speed(out.mkph,1)==0 && out.mkph>0);
    assert(drive_display_speed(300000,0)==186 && drive_display_speed(300000,1)==300);
    assert(!drive_parse(&native,sizeof native-1,&out));
    native.magic=0; assert(!drive_parse(&native,sizeof native,&out));
    native.magic=DRIVE_N57_MAGIC ^ (UINT64_C(1)<<40);
    assert(!drive_parse(&native,sizeof native,&out));
    native.magic=DRIVE_N57_MAGIC; native.mkph=300001;
    assert(!drive_parse(&native,sizeof native,&out));
    native.mkph=-2; assert(!drive_parse(&native,sizeof native,&out));
    native.mkph=-1; native.rpm=-1;
    assert(drive_parse(&native,sizeof native,&out) && out.mkph==-1);
    legacy.kph=256; assert(!drive_parse(&legacy,sizeof legacy,&out));
    puts("Drive IPC: legacy/new versions, fractional speed, bounds and display rounding OK");
    return 0;
}
