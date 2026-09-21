/*
 * Host tests for the real jbuf implementation.
 *   cc -I../../components/webcfg/include -o test_json_buf \
 *      test_json_buf.c ../../components/webcfg/json_buf.c && ./test_json_buf
 *
 * WHAT THIS IS GUARDING. The RC channel list in /api/state was emitted by a
 * chain of `n += snprintf(body + n, cap - n, ...)` with a hand-written bound:
 *
 *     if (n >= (int)sizeof(body) - 8) break;
 *
 * body is a char * parameter, so sizeof took the pointer -- four bytes on this
 * part. The test read `n >= -4`, was true immediately, and the list was
 * truncated to ONE channel for every flight controller that ever connected.
 * The settings page indexes rc[aux + 3], so its live channel readout was dead
 * for every AUX channel, and it did not look like a bug: it looked like a
 * flight controller that was not sending that channel. The firmware's own
 * switch handling reads msp_state_t directly and was unaffected, which is why
 * hardware testing signed off the AUX trigger and never caught it.
 *
 * Section 1 is that exact case. The rest pin the properties the fix relies on.
 */
#include <stdio.h>
#include <string.h>
#include "json_buf.h"

static int fails = 0;
#define CHECK(cond, what) do { \
    if (!(cond)) { printf("  FAIL: %s\n", (what)); fails++; } } while (0)
#define CHECK_STR(got, want) do { \
    if (strcmp((got), (want)) != 0) { \
        printf("  FAIL: got \"%s\", expected \"%s\"\n", (got), (want)); \
        fails++; } } while (0)

int main(void)
{
    char buf[256];
    jbuf_t j;

    printf("JSON buffer\n\n");

    printf("1. REGRESSION: every RC channel is emitted, not just the first\n");
    {
        /* A realistic eight-channel frame. Channel 6 is where an AUX 3 record
         * switch lands, which is exactly what the truncation used to hide. */
        const uint16_t rc[8] = { 1500, 1500, 1000, 1500, 1800, 1500, 1200, 1500 };
        jbuf_init(&j, buf, sizeof(buf));
        jbuf_add_rc(&j, rc, 8);
        CHECK_STR(buf, "1500,1500,1000,1500,1800,1500,1200,1500");
        CHECK(jbuf_ok(&j), "eight channels fit in 256 bytes");

        /* The page reads rc[aux + 3]; prove that index survives the trip. */
        int commas = 0;
        for (const char *p = buf; *p; p++) if (*p == ',') commas++;
        CHECK(commas == 7, "eight values means seven separators");
    }

    printf("2. an eighteen-channel frame is not clipped either\n");
    {
        uint16_t rc[18];
        for (int i = 0; i < 18; i++) rc[i] = (uint16_t)(1000 + i);
        jbuf_init(&j, buf, sizeof(buf));
        jbuf_add_rc(&j, rc, 18);
        CHECK(jbuf_ok(&j), "MSP_MAX_RC_CHANNELS worth of values fit");
        CHECK(strstr(buf, "1017") != NULL, "the last channel is present");
    }

    printf("3. a buffer that really is too small truncates cleanly\n");
    {
        /* The failure that the broken guard was trying to prevent. It must
         * stop on a whole value: half a number is still valid JSON and would
         * be read as a real reading. */
        const uint16_t rc[8] = { 1111, 2222, 3333, 4444, 5555, 6666, 7777, 8888 };
        char small[16];
        jbuf_init(&j, small, sizeof(small));
        jbuf_add_rc(&j, rc, 8);
        CHECK(!jbuf_ok(&j), "overflow is reported, not hidden");
        CHECK(jbuf_len(&j) < sizeof(small), "length stays inside the buffer");
        CHECK(small[jbuf_len(&j)] == '\0', "always NUL-terminated");
        /* Every character written is part of a complete value. */
        size_t n = strlen(small);
        CHECK(n == 0 || (small[n - 1] >= '0' && small[n - 1] <= '9'),
              "never ends mid-separator");
        CHECK(strstr(small, "1111") != NULL, "what fits is still correct");
    }

    printf("4. a refused write appends NOTHING, so the caller can still close\n");
    {
        /* webcfg_scan_json() relies on this: it breaks out on jbuf_full() and
         * then appends "]}". That only works if the write that did not fit
         * left no partial object behind AND left room reserved. */
        char small[12];
        jbuf_init(&j, small, sizeof(small));
        jbuf_addf(&j, "%s", "abcde");
        size_t before = jbuf_len(&j);
        jbuf_addf(&j, "%s", "this will not fit at all");
        CHECK(jbuf_len(&j) == before, "a refused write does not advance len");
        CHECK_STR(small, "abcde");
    }

    printf("5. once truncated it stays truncated\n");
    {
        /* A later short write that happened to fit would splice onto a
         * truncated structure and produce JSON that parses into the WRONG
         * thing -- worse than obvious loss. */
        char small[12];
        jbuf_init(&j, small, sizeof(small));
        jbuf_addf(&j, "%s", "abcdefgh");
        jbuf_addf(&j, "%s", "XXXXXXXXXXXXXXXX");   /* refused */
        jbuf_addf(&j, "%s", "!");                  /* would fit, must not land */
        CHECK(strchr(small, '!') == NULL, "no write lands after an overflow");
        CHECK(!jbuf_ok(&j), "still reported as truncated");
    }

    printf("6. exact fit is not an overflow\n");
    {
        char tight[6];                 /* five characters plus the NUL */
        jbuf_init(&j, tight, sizeof(tight));
        jbuf_addf(&j, "%s", "12345");
        CHECK(jbuf_ok(&j), "a value that exactly fills the buffer is fine");
        CHECK_STR(tight, "12345");
        CHECK(jbuf_len(&j) == 5, "length is the five characters");
    }

    printf("7. zero channels is an empty list, not a malformed one\n");
    {
        jbuf_init(&j, buf, sizeof(buf));
        jbuf_addf(&j, "[");
        jbuf_add_rc(&j, NULL, 0);
        jbuf_addf(&j, "]");
        CHECK_STR(buf, "[]");
        CHECK(jbuf_ok(&j), "an FC with no channels is not an error");
    }

    printf("\n%s\n", fails ? "FAILURES" : "ALL PASS (0 failures)");
    return fails ? 1 : 0;
}
