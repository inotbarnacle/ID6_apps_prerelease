#include "fault_display.h"
#include "fault_preferences.h"
#include <assert.h>
#include <string.h>

int main() {
    FaultLine line;
    assert(parseFaultLine("02ff12  status 09 mask 08",25,line));
    assert(line.code==0x02ff12 && line.status==9 && line.available==8 && line.has_mask);
    assert(!strcmp(faultState(line),"Confirmed")); // unsupported testFailed must not claim Active
    const struct { unsigned status,mask; const char* state; } cases[]={
        {1,1,"Active"},{4,4,"Pending"},{8,8,"Confirmed"},{2,2,"This cycle"},
        {0x20,0x20,"Since clear"},{0x80,0x80,"Warning"},{1,8,"Unknown"},
        {0xff,0,"Unknown"},{0x50,0xff,"Unknown"},{0x2f,0xff,"Active"},
        {0x2e,0xff,"Pending"},{0x2a,0xff,"Confirmed"}
    };
    for (unsigned i=0;i<sizeof cases/sizeof cases[0];++i) {
        line.status=cases[i].status; line.available=cases[i].mask;
        assert(!strcmp(faultState(line),cases[i].state));
    }
    for (unsigned mask=0;mask<256;++mask) for (unsigned raw=0;raw<256;++raw) {
        line.status=raw; line.available=mask;
        const char* unmasked=faultState(line);
        line.status=raw & mask;
        assert(!strcmp(unmasked,faultState(line)));
        if (!(mask & 1)) assert(strcmp(unmasked,"Active"));
    }
    assert(parseFaultLine("000001  status FF",17,line) && !line.has_mask);
    assert(!strcmp(faultState(line),"Raw"));
    assert(parseFaultLine("FFFFFF  status 00 mask ff",25,line) && line.code==0xffffff);
    const char* invalid[]={"-00001  status 01 mask FF","+00001  status 01 mask FF",
        "0x0001  status 01 mask FF","000001  status -1 mask FF","000001  status +1 mask FF",
        "000001  status 0x mask FF","000001  status 01 mask -1","000001  status 01 mask 0x",
        "000001  status 01 mask FF ","000001  STATUS 01 mask FF","000001 status 01 mask FF",
        "000001  status 01 mask F","1000001  status 01 mask FF","","Read rejected (NRC 31)"};
    for (unsigned i=0;i<sizeof invalid/sizeof invalid[0];++i)
        assert(!parseFaultLine(invalid[i],strlen(invalid[i]),line));
    assert(!parseFaultLine(0,25,line));
    const char* valid="000001  status 01 mask FF";
    for (size_t length=0;length<25;++length)
        assert(parseFaultLine(valid,length,line)==(length==17));

    char path[]="/tmp/fault-preferences.XXXXXX";
    int fd=mkstemp(path);
    assert(fd>=0); close(fd);
    FaultPreferences prefs;
    assert(!prefs.raw && !prefs.load(path));
    assert(prefs.raw); // a present but empty settings file fails closed
    prefs.raw=true;
    assert(prefs.save(path));
    FaultPreferences restored;
    assert(restored.load(path) && restored.raw);
    prefs.raw=false;
    assert(prefs.save(path) && restored.load(path) && !restored.raw);
    const unsigned char invalid_prefs[][5]={{'E',2,1,0,0},{'E',1,2,0,0},{'E',1,0,1,0},{'G',1,1,0,0}};
    for (unsigned i=0;i<sizeof invalid_prefs/sizeof invalid_prefs[0];++i) {
        assert(savePersistentBytes(path,invalid_prefs[i],4));
        assert(!restored.load(path) && restored.raw);
    }
    const unsigned char good[]={'E',1,1,0,0};
    for (unsigned length=0;length<=5;++length) {
        if (length==4) continue;
        assert(savePersistentBytes(path,good,length) && !restored.load(path) && restored.raw);
    }
    assert(!prefs.save("/nonexistent-idrive-directory/preferences"));
    unlink(path);
    FaultPreferences missing;
    assert(!missing.load(path) && !missing.raw); // absent settings retain legacy compatibility
    puts("Fault mask interpretation, strict IPC hex parsing, legacy raw status and profile persistence passed.");
}
