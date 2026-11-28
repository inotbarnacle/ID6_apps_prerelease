/* Adapted from the ID6 Apps App Updates worker. See LICENSE. */
#define _POSIX_C_SOURCE 200809L
#define _QNX_SOURCE
#define _DARWIN_C_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef ID6_ROOT
#define ID6_ROOT "/var/opt/hmi/data/ID5"
#define ID6_FS "/fs"
#define ID6_MNT "/mnt"
#define ID6_FRAMEWORKS "/opt/hmi/ID5/lib"
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif

#define PLUGINS ID6_ROOT "/Plugins"
#define UPDATER PLUGINS "/ID6LibraryLab"
#define LOCK ID6_ROOT "/GaugeDeck-install-lock"
#define LOCK_RECORD LOCK "/usb"
#define USB_NAME "ID6Apps.update"
#define SIGNATURE_SIZE 384
#define BUNDLE_LIMIT (32 * 1024 * 1024)
#define PATH_SIZE 512
#define MAX_PAIRS 64
#define PANEL_MASK 31u

enum install_mode { update_installed, install_new };
enum staged_action { leave_unchanged, replace_existing, add_missing };

/* These IDs are part of the signed file format. Append new IDs at the end. */
static const struct app {
    const char *name;
    const char *worker;
    int images;
    const char *extra_worker;
    int allow_new;
} apps[] = {
    {"BoostDeck", "boostdeck-collector-qnx", 1, NULL, 1},
    {"ShiftDeck", "shift-deck-qnx", 1, NULL, 1},
    {"EngineCodes", "engine-codes-qnx", 1, NULL, 1},
    {"MhdMaps", "mhd-maps-qnx", 1, "exhaust-flap-worker-qnx", 1},
    {"Bootmod3Suite", "bootmod3-qnx", 1, NULL, 0},
    {"MgFlasher", "mgflasher-qnx", 1, NULL, 1},
    {"HybridDeck", "hybriddeck-collector-qnx", 1, NULL, 1},
    {"XDriveControl", "xdrive-control-qnx", 0, NULL, 0},
    {"BEAMer", "beamer-qnx", 1, NULL, 1},
    {"RoadJet", "road-jet-qnx", 1, NULL, 1},
    {"LaunchDeck", "shift-deck-qnx", 1, NULL, 1},
    {"HudTest", "hud-shape-qnx", 0, NULL, 1},
    {"WarmDeck", "warmdeck-collector-qnx", 1, NULL, 1},
};
#define APP_COUNT (sizeof apps / sizeof apps[0])

static const char *const panels[] = {"800x480\n", "960x480\n", "1280x480\n", "1440x540\n", "1920x720\n"};

/* No filenames come from the USB. */
static unsigned filenames(const struct app *app, char names[6][64]) {
    snprintf(names[0], 64, "%s.manifest", app->name);
    snprintf(names[1], 64, "%s.xml", app->name);
    snprintf(names[2], 64, "lib%s.so", app->name);
    snprintf(names[3], 64, "%s", app->worker);
    unsigned count = 4;
    if (app->extra_worker) snprintf(names[count++], 64, "%s", app->extra_worker);
    if (app->images) snprintf(names[count++], 64, "%s_ALL_Images.zip", app->name);
    return count;
}

static int is_dir(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Also check the opened object on systems without O_NOFOLLOW. */
static int open_regular(const char *path, struct stat *st) {
    struct stat before;
    if (lstat(path, &before) != 0) return -1;
    if (!S_ISREG(before.st_mode)) {
        errno = EINVAL;
        return -1;
    }
    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW);
    if (fd < 0) return -1;
    if (fstat(fd, st) != 0 || !S_ISREG(st->st_mode) ||
        before.st_dev != st->st_dev || before.st_ino != st->st_ino) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    return fd;
}

static FILE *read_regular(const char *path, struct stat *st) {
    const int fd = open_regular(path, st);
    if (fd < 0) return NULL;
    FILE *file = fdopen(fd, "rb");
    if (!file) close(fd);
    return file;
}

static int consider_usb(const char *mount, int *found, struct stat *selected) {
    if (!is_dir(mount)) return 1;
    char path[PATH_SIZE];
    snprintf(path, sizeof path, "%s/" USB_NAME, mount);
    struct stat st;
    int fd = open_regular(path, &st);
    if (fd < 0) return errno == ENOENT;
    if (*found < 0) {
        *found = fd;
        *selected = st;
        return 1;
    }
    close(fd);
    return selected->st_ino && st.st_dev == selected->st_dev && st.st_ino == selected->st_ino;
}

static int search_mounts(const char *root, const char *prefix, int *fd, struct stat *selected) {
    DIR *dir = opendir(root);
    if (!dir) return errno == ENOENT;
    struct dirent *entry;
    int ok = 1;
    const size_t len = strlen(prefix);
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, prefix, len) || !isdigit((unsigned char)entry->d_name[len]))
            continue;
        const char *end = entry->d_name + len;
        while (isalnum((unsigned char)*end)) ++end;
        if (*end) continue;
        char mount[PATH_SIZE];
        const int size = snprintf(mount, sizeof mount, "%s/%s", root, entry->d_name);
        if (size < 0 || (size_t)size >= sizeof mount || !consider_usb(mount, fd, selected)) {
            ok = 0;
            break;
        }
    }
    closedir(dir);
    return ok;
}

static int copy_bytes(FILE *from, FILE *to, uint32_t size) {
    unsigned char buf[8192];
    while (size) {
        size_t chunk = size < sizeof buf ? size : sizeof buf;
        if (fread(buf, 1, chunk, from) != chunk || fwrite(buf, 1, chunk, to) != chunk)
            return 0;
        size -= chunk;
    }
    return 1;
}

static FILE *create_file(const char *path, mode_t mode) {
    const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, mode);
    if (fd < 0) return NULL;
    FILE *file = fdopen(fd, "wb");
    if (!file) close(fd);
    return file;
}

static int finish_file(FILE *file, int ok) {
    ok = ok && fflush(file) == 0 && fsync(fileno(file)) == 0;
    return fclose(file) == 0 && ok;
}

static int verify(FILE *bundle, long length) {
    struct stat st;
    FILE *key_file = read_regular(UPDATER "/update-key.der", &st);
    if (!key_file) return 0;
    EVP_PKEY *key = st.st_size > 0 && st.st_size <= 1024 ? d2i_PUBKEY_fp(key_file, NULL) : NULL;
    fclose(key_file);
    if (!key) return 0;

    EVP_MD_CTX *ctx = EVP_MD_CTX_create();
    int ok = ctx && EVP_PKEY_size(key) == SIGNATURE_SIZE &&
             EVP_VerifyInit(ctx, EVP_sha256()) == 1;
    unsigned char buf[8192];
    while (ok && length > 0) {
        size_t chunk = length < (long)sizeof buf ? (size_t)length : sizeof buf;
        ok = fread(buf, 1, chunk, bundle) == chunk && EVP_VerifyUpdate(ctx, buf, chunk) == 1;
        length -= chunk;
    }
    unsigned char signature[SIGNATURE_SIZE];
    ok = ok && fread(signature, 1, sizeof signature, bundle) == sizeof signature &&
         EVP_VerifyFinal(ctx, signature, sizeof signature, key) == 1;
    if (ctx) EVP_MD_CTX_destroy(ctx);
    EVP_PKEY_free(key);
    return ok && fseek(bundle, 0, SEEK_SET) == 0;
}

static int framework_hash(const char *path, unsigned char digest[32]) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_create();
    int ok = ctx && EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1;
    unsigned char buf[8192];
    size_t len;
    while (ok && (len = fread(buf, 1, sizeof buf, file)) != 0)
        ok = EVP_DigestUpdate(ctx, buf, len) == 1;
    unsigned int size;
    ok = ok && !ferror(file) && EVP_DigestFinal_ex(ctx, digest, &size) == 1 && size == 32;
    if (ctx) EVP_MD_CTX_destroy(ctx);
    fclose(file);
    return ok;
}

static int read_number(FILE *file, uint32_t *number) {
    uint8_t bytes[4];
    if (fread(bytes, 1, sizeof bytes, file) != sizeof bytes) return 0;
    *number = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
              ((uint32_t)bytes[2] << 8) | bytes[3];
    return 1;
}

static unsigned get_panel(void) {
    struct stat st;
    FILE *file = read_regular(UPDATER "/panel.txt", &st);
    if (!file) return 0;
    char line[16];
    const int ok = fgets(line, sizeof line, file) && fgetc(file) == EOF && !ferror(file);
    fclose(file);
    if (ok) {
        for (unsigned i = 0; i < sizeof panels / sizeof panels[0]; i++)
            if (strcmp(line, panels[i]) == 0) return 1u << i;
    }
    return 0;
}

/* A missing marker is a supported pre-USB install. */
static int older_version(const char *live, uint32_t version) {
    struct stat st;
    char path[PATH_SIZE];
    snprintf(path, sizeof path, "%s/.id6-usb-version", live);
    FILE *file = read_regular(path, &st);
    if (!file) return errno == ENOENT ? 1 : -1;
    uint32_t previous;
    const int ok = read_number(file, &previous) && fgetc(file) == EOF && !ferror(file);
    fclose(file);
    return ok ? previous < version : -1;
}

static int stage_settings(const char *live, const char *stage, uint32_t version) {
    char path[PATH_SIZE];
    struct stat st;
    snprintf(path, sizeof path, "%s/gateway-ip.txt", live);
    FILE *source = read_regular(path, &st);
    if (!source && errno != ENOENT) return 0;
    if (source) {
        snprintf(path, sizeof path, "%s/gateway-ip.txt", stage);
        FILE *dest = st.st_size >= 0 && st.st_size <= 512 ? create_file(path, 0644) : NULL;
        const int ok = dest && finish_file(dest, copy_bytes(source, dest, (uint32_t)st.st_size));
        fclose(source);
        if (!ok) return 0;
    }
    snprintf(path, sizeof path, "%s/.id6-usb-version", stage);
    FILE *file = create_file(path, 0644);
    const uint8_t bytes[] = {version >> 24, version >> 16, version >> 8, version};
    return file && finish_file(file, fwrite(bytes, 1, sizeof bytes, file) == sizeof bytes);
}

static int stage_entry(FILE *bundle, long end, unsigned app_id, const char *stage) {
    const struct app *app = &apps[app_id];
    char names[6][64];
    const unsigned count = filenames(app, names);
    for (unsigned i = 0; i < count; i++) {
        uint32_t size;
        if (!read_number(bundle, &size) || !size) return 0;
        const long offset = ftell(bundle);
        if (offset < 0 || offset >= end || size > (unsigned long)(end - offset)) return 0;
        if (!stage) {
            if (fseek(bundle, size, SEEK_CUR) != 0) return 0;
            continue;
        }
        char path[PATH_SIZE];
        snprintf(path, sizeof path, "%s/%s", stage, names[i]);
        const mode_t mode = i == 3 || (i == 4 && app->extra_worker) ? 0755 : 0644;
        FILE *file = create_file(path, mode);
        if (!file || !finish_file(file, copy_bytes(bundle, file, size))) return 0;
    }
    return 1;
}

static int installed(const struct app *app, const char *live) {
    struct stat st;
    if (lstat(live, &st) != 0) return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode)) return -1;
    char path[PATH_SIZE];
    snprintf(path, sizeof path, "%s/%s.manifest", live, app->name);
    return lstat(path, &st) == 0 && S_ISREG(st.st_mode) ? 1 : -1;
}

static void remove_stage(const char *tx, unsigned app_id) {
    const struct app *app = &apps[app_id];
    char names[6][64], path[PATH_SIZE];
    const unsigned count = filenames(app, names);
    for (unsigned i = 0; i < count; i++) {
        snprintf(path, sizeof path, "%s/new-%s/%s", tx, app->name, names[i]);
        unlink(path);
    }
    snprintf(path, sizeof path, "%s/new-%s/gateway-ip.txt", tx, app->name);
    unlink(path);
    snprintf(path, sizeof path, "%s/new-%s/.id6-usb-version", tx, app->name);
    unlink(path);
    snprintf(path, sizeof path, "%s/new-%s", tx, app->name);
    rmdir(path);
}

static void app_paths(const char *tx, unsigned app_id, char *live, char *stage, char *backup) {
    const char *name = apps[app_id].name;
    snprintf(live, PATH_SIZE, PLUGINS "/%s", name);
    snprintf(stage, PATH_SIZE, "%s/new-%s", tx, name);
    snprintf(backup, PATH_SIZE, "%s/old-%s", tx, name);
}

/* A power interruption leaves the shared lock and transaction on disk for recovery. */
static int publish(const char *tx, const enum staged_action *staged) {
    unsigned i;
    int saved[APP_COUNT] = {0}, moved[APP_COUNT] = {0};
    char live[PATH_SIZE], stage[PATH_SIZE], backup[PATH_SIZE];
    sync();
    for (i = 0; i < APP_COUNT; i++) {
        if (!staged[i]) continue;
        app_paths(tx, i, live, stage, backup);
        if (staged[i] == replace_existing) {
            if (installed(&apps[i], live) != 1 || rename(live, backup) != 0) break;
            saved[i] = 1;
            sync(); /* Persist the backup before exposing the replacement. */
        } else if (installed(&apps[i], live) != 0) {
            break;
        }
        if (rename(stage, live) != 0) break;
        moved[i] = 1;
    }
    if (i == APP_COUNT) {
        sync();
        return 1;
    }

    int restored = 1;
    for (int j = (int)i; j >= 0; j--) {
        if (!moved[j] && !saved[j]) continue;
        app_paths(tx, (unsigned)j, live, stage, backup);
        if (moved[j] && rename(live, stage) != 0) {
            restored = 0;
            continue;
        }
        if (saved[j] && rename(backup, live) != 0) restored = 0;
    }
    sync();
    return restored ? 0 : -1;
}

static const char *apply(FILE *file, long end, const char *tx, enum install_mode mode, int *count, int *keep_lock) {
    const char *invalid = "The USB update has an invalid format.";
    char magic[8];
    uint32_t version, pair_count;
    if (fread(magic, 1, sizeof magic, file) != sizeof magic ||
        memcmp(magic, "ID6UPD1\n", sizeof magic) != 0 ||
        !read_number(file, &version) || !version ||
        !read_number(file, &pair_count) || !pair_count || pair_count > MAX_PAIRS)
        return invalid;

    unsigned char actual[64], candidate[64];
    if (!framework_hash(ID6_FRAMEWORKS "/libAppFramework.A.so", actual) ||
        !framework_hash(ID6_FRAMEWORKS "/libCoreFramework.A.so", actual + 32))
        return "Could not check the iDrive framework.";
    int match = 0;
    for (uint32_t i = 0; i < pair_count; i++) {
        if (fread(candidate, 1, sizeof candidate, file) != sizeof candidate) return invalid;
        if (memcmp(actual, candidate, sizeof actual) == 0) match = 1;
    }
    if (!match) return "This update does not support your iDrive firmware.";

    const unsigned panel = get_panel();
    if (!panel) return "Display profile missing. Restage the Library.";
    enum staged_action staged[APP_COUNT] = {leave_unchanged};
    unsigned seen[APP_COUNT] = {0};
    long offset;
    while ((offset = ftell(file)) >= 0 && offset < end) {
        uint32_t id, mask;
        if (!read_number(file, &id) || id >= APP_COUNT || !read_number(file, &mask) ||
            !mask || (mask & ~PANEL_MASK) || (seen[id] & mask))
            return invalid;
        seen[id] |= mask;
        const char *target = NULL;
        char live[PATH_SIZE], stage[PATH_SIZE], backup[PATH_SIZE];
        if ((panel & mask) && (mode == update_installed || apps[id].allow_new)) {
            app_paths(tx, id, live, stage, backup);
            const int present = installed(&apps[id], live);
            if (present < 0) return "An app directory could not be checked.";
            const int eligible = mode == install_new ? present == 0 :
                                 present == 1 ? older_version(live, version) : 0;
            if (eligible < 0) return "An installed app could not be checked.";
            if (eligible) {
                if (mkdir(stage, 0755) != 0 || !stage_settings(live, stage, version))
                    return "Could not prepare app files. Check free space and try again.";
                target = stage;
                staged[id] = present ? replace_existing : add_missing;
                ++*count;
            }
        }
        if (!stage_entry(file, end, id, target))
            return "Could not stage the update. Check free space and the update file.";
    }
    if (offset != end) return invalid;
    if (!*count) return mode == install_new ? "No supported new apps in this package." :
                                              "No matching installed apps need this update.";

    const int published = publish(tx, staged);
    if (published == 1) return NULL;
    if (published == 0) return "Update failed. Original apps were restored.";
    *keep_lock = 1;
    return "Update interrupted. Backups kept. Check with the desktop installer.";
}

static int create_transaction(char path[PATH_SIZE]) {
    const unsigned long tick = (unsigned long)time(NULL);
    for (unsigned attempt = 0; attempt < 100; attempt++) {
        snprintf(path, PATH_SIZE, ID6_ROOT "/USBUpdate-%lu-%ld-%u", tick, (long)getpid(), attempt);
        if (mkdir(path, 0700) == 0) return 1;
        if (errno != EEXIST) break;
    }
    return 0;
}

static const char *update(enum install_mode mode, int *count) {
    struct stat st;
    int fd = -1;
    if (!search_mounts(ID6_FS, "usb", &fd, &st) || !search_mounts(ID6_MNT, "umass", &fd, &st)) {
        if (fd >= 0) close(fd);
        return "Use one readable USB drive with ID6Apps.update.";
    }
    if (fd < 0) return "Put ID6Apps.update at the top of the USB drive.";
    if (st.st_size <= SIGNATURE_SIZE || st.st_size > BUNDLE_LIMIT) {
        close(fd);
        return "The USB update file has an invalid size.";
    }
    FILE *media = fdopen(fd, "rb");
    if (!media) {
        close(fd);
        return "Could not read the USB drive.";
    }
    if (mkdir(LOCK, 0755) != 0) {
        const int busy = errno == EEXIST;
        fclose(media);
        return busy ? "An update or install is pending. Check the desktop installer."
                               : "iDrive app storage is not writable.";
    }

    char tx[PATH_SIZE];
    const char *result = "Could not prepare iDrive app storage.";
    int keep_lock = 0;
    if (is_dir(PLUGINS) && create_transaction(tx)) {
        char snapshot[PATH_SIZE];
        snprintf(snapshot, sizeof snapshot, "%s/bundle", tx);
        FILE *owner = create_file(LOCK_RECORD, 0644);
        const int recorded = owner && finish_file(owner, fprintf(owner, "%s\n", strrchr(tx, '/') + 1) > 0);
        FILE *local = recorded ? fopen(snapshot, "w+b") : NULL;
        if (local) {
            /* Parse the verified private copy, even if the USB is pulled or modified. */
            if (unlink(snapshot) == 0 && copy_bytes(media, local, (uint32_t)st.st_size) &&
                fgetc(media) == EOF && !ferror(media) && fflush(local) == 0 &&
                fseek(local, 0, SEEK_SET) == 0) {
                const long end = (long)st.st_size - SIGNATURE_SIZE;
                result = verify(local, end) ? apply(local, end, tx, mode, count, &keep_lock)
                                            : "Update signature failed. Use the ID6 Apps release file.";
            } else {
                result = "Could not copy the update. Check the USB drive and free space.";
            }
            fclose(local);
            unlink(snapshot);
        }
        if (result && !keep_lock) {
            for (unsigned i = 0; i < APP_COUNT; i++) remove_stage(tx, i);
            rmdir(tx);
        }
    }
    fclose(media);
    if (!keep_lock) {
        if ((unlink(LOCK_RECORD) != 0 && errno != ENOENT) || rmdir(LOCK) != 0)
            return "Update lock could not be cleared. Check the desktop installer.";
    }
    return result;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    enum install_mode mode;
    if (!strcmp(argv[1], "--update")) mode = update_installed;
    else if (!strcmp(argv[1], "--install-new")) mode = install_new;
    else return 2;
    umask(022);
    int count = 0;
    const char *result = update(mode, &count);
    if (result) puts(result);
    else printf("%s %d %s. Restart iDrive.\n", mode == install_new ? "Installed" : "Updated",
                count, count == 1 ? "app" : "apps");
    return result != NULL;
}
