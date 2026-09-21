/*
 * See json_buf.h for why the accounting lives here rather than at each call
 * site.
 */
#include "json_buf.h"

#include <stdio.h>

void jbuf_init(jbuf_t *j, char *buf, size_t cap)
{
    j->buf  = buf;
    j->cap  = cap;
    j->len  = 0;
    j->over = false;
    if (cap > 0) buf[0] = '\0';
}

void jbuf_addf(jbuf_t *j, const char *fmt, ...)
{
    if (j->cap == 0) { j->over = true; return; }

    /* Once something has been refused, stop: a later write that happened to
     * fit would splice itself onto a truncated structure and produce JSON
     * that parses into the wrong thing, which is worse than obvious loss. */
    if (j->over) return;

    size_t room = j->cap - j->len;          /* >= 1, since len < cap always  */

    va_list ap;
    va_start(ap, fmt);
    int want = vsnprintf(j->buf + j->len, room, fmt, ap);
    va_end(ap);

    if (want < 0) {                          /* encoding error               */
        j->buf[j->len] = '\0';
        j->over = true;
        return;
    }
    if ((size_t)want >= room) {
        /* Did not fit. vsnprintf has already truncated into the buffer, so
         * put the terminator back where it was and report the loss rather
         * than leaving a half-written value behind. */
        j->buf[j->len] = '\0';
        j->over = true;
        return;
    }
    j->len += (size_t)want;
}

void jbuf_add_rc(jbuf_t *j, const uint16_t *rc, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
        jbuf_addf(j, "%s%u", i ? "," : "", (unsigned)rc[i]);
        if (jbuf_full(j)) return;
    }
}
