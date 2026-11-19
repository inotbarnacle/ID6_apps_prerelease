#include "launch_records.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void writeRaw(const char* path, const unsigned char* data, size_t size) {
    FILE* file = fopen(path, "wb");
    fwrite(data, 1, size, file);
    fclose(file);
}

static void test_missing_file_has_no_records(const char* path) {
    unlink(path);
    LaunchRecords records = loadLaunchRecords(path);
    CHECK(records.best_ms == 0 && records.last_ms == 0);
}

static void test_round_trip(const char* path) {
    LaunchRecords saved = {5123, 5480};
    CHECK(saveLaunchRecords(path, saved));
    LaunchRecords loaded = loadLaunchRecords(path);
    CHECK(loaded.best_ms == 5123 && loaded.last_ms == 5480);
}

static void test_rejects_corrupt_files(const char* path) {
    const unsigned char wrongMagic[kLaunchRecordBytes] = {'X', 1, 1, 0, 0, 0, 1, 0, 0, 0};
    writeRaw(path, wrongMagic, sizeof wrongMagic);
    CHECK(loadLaunchRecords(path).best_ms == 0);

    const unsigned char truncated[4] = {'L', 1, 1, 0};
    writeRaw(path, truncated, sizeof truncated);
    CHECK(loadLaunchRecords(path).best_ms == 0);

    const unsigned char tooLong[kLaunchRecordBytes + 1] = {'L', 1, 100, 0, 0, 0, 100, 0, 0, 0, 7};
    writeRaw(path, tooLong, sizeof tooLong);
    CHECK(loadLaunchRecords(path).best_ms == 0);

    unsigned char implausible[kLaunchRecordBytes] = {'L', 1};
    writeRecord(implausible + 2, LT_TIMEOUT_MS + 1);
    writeRaw(path, implausible, sizeof implausible);
    CHECK(loadLaunchRecords(path).best_ms == 0);
}

static void test_finished_run_updates_records_without_mutating_input(void) {
    const LaunchRecords none = {0, 0};
    LaunchRecords first = withFinishedRun(none, 5600);
    CHECK(first.best_ms == 5600 && first.last_ms == 5600);
    CHECK(none.best_ms == 0 && none.last_ms == 0);

    LaunchRecords slower = withFinishedRun(first, 5900);
    CHECK(slower.best_ms == 5600 && slower.last_ms == 5900);

    LaunchRecords faster = withFinishedRun(slower, 5300);
    CHECK(faster.best_ms == 5300 && faster.last_ms == 5300);
}

int main() {
    char path[] = "/tmp/launch-records-test-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 1;
    close(fd);
    test_missing_file_has_no_records(path);
    test_round_trip(path);
    test_rejects_corrupt_files(path);
    test_finished_run_updates_records_without_mutating_input();
    unlink(path);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("launch records: all tests passed");
    return 0;
}
