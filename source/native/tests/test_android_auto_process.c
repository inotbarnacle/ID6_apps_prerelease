#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include "android_auto_process.h"

static void expect(const char *listing, int expected) {
    FILE *stream = tmpfile();
    assert(stream);
    assert(fputs(listing, stream) >= 0);
    rewind(stream);
    assert(android_auto_process_listing(stream) == expected);
    fclose(stream);
}

int main(void) {
    expect("     pid Arguments\n 3620969 /opt/aa/nbt-aad\n", AA_PROCESS_RUNNING);
    expect("123 /var/opt/nbtapp/apps/aa/1.9.116/nbt-aad --flag\n", AA_PROCESS_RUNNING);
    expect("123 nbt-aad\n", AA_PROCESS_RUNNING);
    expect("1 /proc/boot/procnto\n456 grep nbt-aad\n789 /bin/sh -c /opt/aa/nbt-aad\n",
           AA_PROCESS_STOPPED);
    expect("1 /opt/aa/nbt-aad-helper\n2 /opt/aa/nbt-aad.old\n", AA_PROCESS_STOPPED);
    expect("", AA_PROCESS_UNKNOWN);
    expect("pid Arguments\n", AA_PROCESS_UNKNOWN);
    expect("123\n", AA_PROCESS_UNKNOWN);
    expect("1 /proc/boot/procnto", AA_PROCESS_STOPPED);
    assert(android_auto_process_listing(NULL) == AA_PROCESS_UNKNOWN);
    FILE *unreadable = fopen("/dev/null", "w");
    assert(unreadable);
    assert(android_auto_process_listing(unreadable) == AA_PROCESS_UNKNOWN);
    fclose(unreadable);
    char listing[2200];
    strcpy(listing, "123 /bin/sh ");
    memset(listing + strlen(listing), ' ', 1023 - strlen(listing));
    strcpy(listing + 1023, "456 /opt/aa/nbt-aad\n1 /proc/boot/procnto\n");
    expect(listing, AA_PROCESS_STOPPED);
    memset(listing, 'x', 1000);
    memcpy(listing, "123 /", 5);
    listing[1000] = '\n'; listing[1001] = 0;
    expect(listing, AA_PROCESS_UNKNOWN);
    assert(!android_auto_process_blocks_most(AA_PROCESS_STOPPED, 0));
    assert(android_auto_process_blocks_most(AA_PROCESS_RUNNING, 0));
    assert(android_auto_process_blocks_most(AA_PROCESS_UNKNOWN, 0));
    assert(android_auto_process_blocks_most(AA_PROCESS_STOPPED, 127 << 8));
    assert(android_auto_process_blocks_most(AA_PROCESS_STOPPED, -1));
    puts("Android Auto process guard tests passed");
    return 0;
}
