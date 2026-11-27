#include "boostdeck_assignments.h"
#include <assert.h>
#include <unistd.h>

int main() {
    char path[]="/tmp/boost-assign-XXXXXX";
    int fd=mkstemp(path);assert(fd>=0);close(fd);
    unsigned char signals[2][kBoostDeckSlots];bool diesel,hidden;
    loadBoostDeckAssignments("/not-present",signals,diesel,hidden,true);
    assert(!diesel && !hidden && signals[1][1]==BD_TORQUE);
    unsigned char b4[59]={'B',4,1,1,1};
    for (unsigned p=0;p<2;++p) for (unsigned page=0;page<3;++page)
        for (unsigned i=0;i<9;++i) b4[5+(p*3+page)*9+i]=static_cast<unsigned char>(page==1 ? BD_REL_TORQUE_DDE : BD_RPM);
    assert(savePersistentBytes(path,b4,sizeof b4));
    loadBoostDeckAssignments(path,signals,diesel,hidden,true);
    assert(diesel && hidden && signals[1][8]==BD_REL_TORQUE_DDE);
    assert(boostDeckSelectionMask(signals[1],true,false,true)==BD_BIT(BD_REL_TORQUE_DDE));
    assert(saveBoostDeckAssignments(path,signals,diesel,hidden));
    unsigned char bytes[22];FILE* f=fopen(path,"rb");
    assert(f && fread(bytes,1,sizeof bytes,f)==sizeof bytes && fgetc(f)==EOF);fclose(f);
    assert(bytes[1]==5);
    loadBoostDeckAssignments(path,signals,diesel,hidden,true);
    assert(diesel && hidden && signals[1][8]==BD_REL_TORQUE_DDE);
    b4[4]=3;assert(savePersistentBytes(path,b4,sizeof b4));
    loadBoostDeckAssignments(path,signals,diesel,hidden,true);
    assert(!diesel && !hidden && signals[1][1]==BD_TORQUE);
    signals[0][0]=BD_LOAD;signals[1][0]=BD_LOAD;
    assert(migrateN57EngineLoad(signals) && signals[0][0]==BD_LOAD && signals[1][0]==BD_TORQUE);
    assert(boostDeckSignalHidden(BD_LOAD,true,false,true));
    assert(!boostDeckSignalHidden(BD_LOAD,false,false,true));
    assert(boostDeckSignalHidden(BD_ADBLUE,true,true,true));
    assert(!boostDeckSignalHidden(BD_ADBLUE,true,false,true));
    for (unsigned version=1;version<=3;++version) {
        unsigned char legacy[25]={'B',static_cast<unsigned char>(version),1,1};
        const unsigned offset=version==3 ? 4 : 3,stride=version==1 ? 11 : 9;
        const size_t length=offset+2*stride;
        for (unsigned p=0;p<2;++p) for (unsigned i=0;i<stride;++i)
            legacy[offset+p*stride+i]=static_cast<unsigned char>(i==0 ? BD_BATTERY : BD_RPM);
        assert(savePersistentBytes(path,legacy,length));
        loadBoostDeckAssignments(path,signals,diesel,hidden,true);
        assert(diesel && hidden==(version==3) && signals[1][0]==BD_BATTERY);
        assert(savePersistentBytes(path,legacy,length-1));
        loadBoostDeckAssignments(path,signals,diesel,hidden,true);
        assert(!diesel && !hidden && signals[1][0]==BD_BOOST);
    }
    assert(unlink(path)==0);
    puts("BoostDeck single-panel settings: B1-B5 validation, B4 startup migration and N57 availability OK");
}
