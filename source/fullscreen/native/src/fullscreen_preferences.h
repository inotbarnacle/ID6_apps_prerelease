#ifndef ID6_FULLSCREEN_PREFERENCES_H
#define ID6_FULLSCREEN_PREFERENCES_H
#include "gauge_preferences.h"

inline bool saveFullScreenRecord(const char* path,const unsigned char* data,size_t count) {
#ifdef ID6_EMULATOR
    // QNX shared memory does not support atomic rename. Physical files keep it.
    FILE* file=fopen(path,"wb");
    if (!file) return false;
    const bool saved=fwrite(data,1,count,file)==count;
    return fclose(file)==0 && saved;
#else
    return savePersistentBytes(path,data,count);
#endif
}
#endif
