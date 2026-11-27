/* Experimental N57 DDE measured values. Catalog provenance: docs/N57.md. */
#ifndef ID6_N57_H
#define ID6_N57_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef ID6_N57_SETTINGS
#define ID6_N57_SETTINGS "/var/opt/hmi/data/ID5/n57-sgbd.txt"
#endif
typedef int (*n57_exchange_fn)(void *, unsigned, const unsigned char *, unsigned,
                               const unsigned char *, unsigned, unsigned char *, unsigned,
                               unsigned, unsigned *);
unsigned n57_index_file(const char *path);
/* 1 pinned, 0 absent, -1 invalid/unreadable; absence alone allows the legacy profile. */
int n57_settings(unsigned *index);
unsigned n57_parse_index(const char *text);
int n57_decode(unsigned did, const unsigned char *p, size_t len, double *out);
/* 1 valid/matching, 0 refused/invalid, negative transport failure. Callback includes reply echo.
 * Caller must hold the diagnostic lease across identity verification and each whole
 * dynamic read/pair, on HSFZ or MOST. */
int n57_match(n57_exchange_fn, void *, unsigned expected);
int n57_read(n57_exchange_fn, void *, unsigned did, double *out);
/* One F303 definition/read for RPM 5955 and fractional speed 4C14; exact four-byte data. */
int n57_read_drive(n57_exchange_fn, void *, double *rpm, double *speed);
/* Verified EGS 0F14B0: selector 4306 plus DA27 display/lever/button. DA2E is actual gear. */
int n57_park(n57_exchange_fn, void *);
#ifdef __cplusplus
}
#endif
#endif
