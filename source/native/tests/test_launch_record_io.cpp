// A real failing stdio stream on the Linux CI host, not a mocked return value.
#include "gauge_preferences.h"
#include "launch_timer.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#if defined(__GLIBC__)
struct FailedInput {
    size_t offset;
    unsigned errors, closes;
    bool partial;
};
static FailedInput* active=0;
static ssize_t read_failed(void* pointer,char* output,size_t size) {
    FailedInput* input=static_cast<FailedInput*>(pointer);
    const unsigned char legacy[]={'L',1,0x88,0x13,0,0,0x70,0x17,0,0};
    if (input->partial && input->offset<sizeof legacy) {
        const size_t left=sizeof legacy-input->offset;
        const size_t copied=size<left ? size : left;
        memcpy(output,legacy+input->offset,copied); input->offset+=copied;
        return static_cast<ssize_t>(copied);
    }
    ++input->errors; errno=EIO; return -1;
}
static int close_failed(void* pointer) { ++static_cast<FailedInput*>(pointer)->closes; return 0; }
static FILE* records_open(const char* path,const char* mode) {
    if (!active) return fopen(path,mode);
    const cookie_io_functions_t functions={read_failed,0,0,close_failed};
    return fopencookie(active,mode,functions);
}
#define fopen records_open
#include "launch_records.h"
#undef fopen

int main() {
    for (unsigned partial=0;partial<2;++partial) {
        FailedInput input={0,0,0,partial!=0}; active=&input;
        const LaunchRecords records=loadLaunchRecords("/simulated-record-stream");
        active=0;
        if (records.best_ms || records.last_ms || records.previous_ms || records.third_ms ||
            input.errors!=1 || input.closes!=1) {
            fprintf(stderr,"failed record stream: partial=%u errors=%u closes=%u; expected one failing read and no records\n",
                    partial,input.errors,input.closes);
            return 1;
        }
    }
    puts("LaunchRecords: immediate/partial read errors stop further reads, return no records and close once.");
}
#else
int main() { puts("LaunchRecords fault-stream fixture requires glibc; exercised on Linux CI."); }
#endif
