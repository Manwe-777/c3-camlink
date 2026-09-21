/*
 * Bounded append into a fixed buffer, kept free of ESP-IDF so the host tests
 * exercise this exact code rather than a copy of it.
 *
 * WHY THIS EXISTS. The JSON builders in webcfg.c grew as a chain of
 *
 *     n += snprintf(body + n, cap - n, ...);
 *
 * which is wrong in a way that does not show up until it matters. snprintf
 * returns the length it WOULD have written, so n can pass cap; `cap - n` is
 * size_t, so it then underflows to an enormous value instead of going
 * negative, and `body + n` is already past the end. The bounds check that is
 * supposed to stop that has to be written by hand at every call site, and one
 * of them was written as
 *
 *     if (n >= (int)sizeof(body) - 8) break;
 *
 * where body is a char * parameter. sizeof took the POINTER -- four bytes on
 * this part -- so the test read `n >= -4`, was true on the first pass, and
 * truncated the RC channel list to a single entry. The settings page reads
 * rc[aux + 3], so its live channel readout could never work, while the
 * firmware's own switch handling was fine because it never goes through JSON.
 * Nothing failed loudly; a feature was simply always dead.
 *
 * So the accounting is done once, here, where it can be tested: jbuf tracks
 * its own length, refuses to write past the end, and REMEMBERS that it had to.
 * A caller that wants to stop early asks jbuf_full(); a caller that wants to
 * know whether the result is complete asks jbuf_ok().
 */
#ifndef WEBCFG_JSON_BUF_H
#define WEBCFG_JSON_BUF_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char  *buf;
    size_t cap;      /* including the NUL                                    */
    size_t len;      /* bytes written, never >= cap                          */
    bool   over;     /* something did not fit; the content is truncated      */
} jbuf_t;

/* cap must be at least 1. The buffer is NUL-terminated at every point. */
void jbuf_init(jbuf_t *j, char *buf, size_t cap);

/* Append printf-style. A write that does not fit sets `over` and appends
 * nothing -- a half-written JSON object is worse than a short one, because it
 * fails to parse and the page then shows nothing at all. */
void jbuf_addf(jbuf_t *j, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Bytes written so far, and whether everything asked for actually fit. */
static inline size_t jbuf_len(const jbuf_t *j) { return j->len; }
static inline bool   jbuf_ok(const jbuf_t *j)  { return !j->over; }

/* True once a write has been refused. Loops that emit one element per
 * iteration test this to stop early rather than spinning to the end. */
static inline bool jbuf_full(const jbuf_t *j) { return j->over; }

/* The RC channel list, as the bare comma-separated body of a JSON array --
 * no brackets, so the caller places it.
 *
 * Every channel the flight controller reports, not a convenient prefix of
 * them: a user's arm and record switches sit on high channels, and truncating
 * this list is what hid them. Stops on the last value that fits whole.
 */
void jbuf_add_rc(jbuf_t *j, const uint16_t *rc, unsigned count);

#endif /* WEBCFG_JSON_BUF_H */
