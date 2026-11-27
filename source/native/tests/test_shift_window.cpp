#include "shift_window.h"
#include <assert.h>
#include <limits.h>
#include <string.h>

static void writeBytes(const char* path,const unsigned char* bytes,size_t length) {
    FILE* file=fopen(path,"wb");assert(file);
    assert(fwrite(bytes,1,length,file)==length && fclose(file)==0);
}

int main() {
    ShiftWindow window;
    assert(window.valid() && window.lower==4600 && window.upper==4800);
    assert(window.signal(4599,true)==ShiftWindow::Normal);
    assert(window.signal(4600,true)==ShiftWindow::Warning);
    assert(window.signal(4799,true)==ShiftWindow::Warning);
    assert(window.signal(4800,true)==ShiftWindow::Strong);
    assert(window.signal(6000,true)==ShiftWindow::Strong);
    assert(window.signal(6000,false)==ShiftWindow::Normal && window.signal(-1,true)==ShiftWindow::Normal);
    assert(window.adjustLower(100) && window.lower==4700);
    assert(!window.adjustLower(100) && window.lower==4700);
    assert(!window.adjustUpper(-100) && window.upper==4800);
    assert(!window.adjustLower(50) && !window.adjustUpper(50));
    assert(!window.adjustLower(INT_MAX) && !window.adjustUpper(INT_MIN));
    assert(window.adjustLower(-1700) && window.lower==3000);
    assert(!window.adjustLower(-100) && window.lower==3000);
    assert(window.adjustUpper(700) && window.upper==5500);
    assert(!window.adjustUpper(100) && window.upper==5500);
    assert(window.adjustLower(2000) && window.lower==5000);
    assert(!window.adjustLower(100) && window.lower==5000);

    char path[]="/tmp/id6-shift-window-XXXXXX";
    int fd=mkstemp(path);assert(fd>=0);close(fd);unlink(path);
    assert(!window.load(path) && window.lower==5000 && window.upper==5500);
    assert(window.save(path));
    ShiftWindow loaded;assert(loaded.load(path) && loaded.lower==5000 && loaded.upper==5500);
    FILE* file=fopen(path,"rb");assert(file);
    unsigned char bytes[5]={0};assert(fread(bytes,1,sizeof bytes,file)==4);fclose(file);
    assert(bytes[0]=='W' && bytes[1]==1 && bytes[2]==50 && bytes[3]==55);
    window.lower=window.upper;
    assert(!window.valid() && !window.save(path) && !window.adjustUpper(100));
    assert(loaded.load(path) && loaded.lower==5000 && loaded.upper==5500);

    const unsigned char invalid[][5]={
        {'X',1,46,48,0},{'W',2,46,48,0},{'W',1,29,48,0},{'W',1,51,55,0},
        {'W',1,46,46,0},{'W',1,48,46,0},{'W',1,46,56,0},{'W',1,0,255,0}
    };
    for (unsigned i=0;i<sizeof invalid/sizeof invalid[0];++i) {
        writeBytes(path,invalid[i],4);
        assert(!loaded.load(path) && loaded.lower==5000 && loaded.upper==5500);
    }
    const unsigned char good[]={'W',1,46,48,0};
    for (unsigned length=0;length<=5;++length) {
        writeBytes(path,good,length);
        const bool read=loaded.load(path);
        assert(read==(length==4));
    }
    assert(loaded.lower==4600 && loaded.upper==4800);
    assert(!loaded.save("/this-directory-does-not-exist/ShiftDeck.n57.window.prefs"));
    assert(loaded.lower==4600 && loaded.upper==4800);
    unlink(path);
    puts("N57 shift window: thresholds, bounds, strict separate preferences and atomic persistence OK");
}
