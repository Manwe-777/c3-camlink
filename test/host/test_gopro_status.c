/*
 * The GoPro status walker, in both id families.
 *
 * WHY. The MISSION 1 series carries status ids as two bytes under its own
 * query ids. A walker that only knew the 1-byte layout would read a MISSION 1
 * push as garbage -- id 0, length "hi byte of the id" -- and either invent
 * values or run off the end of the frame. Nobody on this bench has a MISSION
 * 1, so the walker is pure and pinned here against hand-built frames instead.
 */
#include <stdio.h>
#include <string.h>
#include "gopro_cam.h"

static int fails = 0;
#define CHECK(cond, what) do { \
    if (!(cond)) { printf("  FAIL: %s\n", (what)); fails++; } } while (0)

int main(void)
{
    printf("=== GoPro status walker ===\n\n");

    printf("1. the 1-byte family, as a HERO (2024) actually pushes it\n");
    {
        /* status 10 (encoding) = 1, status 97 (preset id) = 0x00010000 */
        const uint8_t f[] = { 10, 1, 0x01,  97, 4, 0x00, 0x01, 0x00, 0x00 };
        size_t i = 0; uint16_t id; uint8_t len; const uint8_t *v;
        CHECK(gopro_status_next(f, sizeof(f), false, &i, &id, &len, &v), "first element");
        CHECK(id == 10 && len == 1 && v[0] == 1, "encoding = 1");
        CHECK(gopro_status_next(f, sizeof(f), false, &i, &id, &len, &v), "second element");
        CHECK(id == 97 && len == 4 && v[1] == 0x01, "preset id 0x10000");
        CHECK(!gopro_status_next(f, sizeof(f), false, &i, &id, &len, &v), "then the end");
        CHECK(i == sizeof(f), "cursor rests at the end");
    }

    printf("2. the 2-byte family: [id hi][id lo][len][value]\n");
    {
        const uint8_t f[] = { 0x00, 10, 1, 0x01,  0x00, 97, 4, 0x00, 0x01, 0x00, 0x00,  0x01, 0x02, 1, 0x07 };
        size_t i = 0; uint16_t id; uint8_t len; const uint8_t *v;
        CHECK(gopro_status_next(f, sizeof(f), true, &i, &id, &len, &v) && id == 10 && v[0] == 1, "encoding");
        CHECK(gopro_status_next(f, sizeof(f), true, &i, &id, &len, &v) && id == 97 && len == 4, "preset id");
        CHECK(gopro_status_next(f, sizeof(f), true, &i, &id, &len, &v) && id == 0x0102 && v[0] == 7, "an id above 255 survives");
        CHECK(!gopro_status_next(f, sizeof(f), true, &i, &id, &len, &v), "then the end");
    }

    printf("3. BLOCKER: a truncated element stops the walk, it is not read\n");
    {
        const uint8_t f[] = { 10, 1, 0x01,  35, 4, 0x00, 0x1B };   /* 35 claims 4 bytes, has 2 */
        size_t i = 0; uint16_t id; uint8_t len; const uint8_t *v;
        CHECK(gopro_status_next(f, sizeof(f), false, &i, &id, &len, &v) && id == 10, "the whole one is read");
        CHECK(!gopro_status_next(f, sizeof(f), false, &i, &id, &len, &v), "the cut one is not");
        CHECK(i == 3, "and the cursor did not move onto it");
        /* A wide header cut in half: two bytes where three are needed. */
        const uint8_t w[] = { 0x00, 10 };
        i = 0;
        CHECK(!gopro_status_next(w, sizeof(w), true, &i, &id, &len, &v), "a half header is refused");
        CHECK(!gopro_status_next(NULL, 0, true, &i, &id, &len, &v), "no frame at all");
    }

    printf("4. a 1-byte frame read as wide does not run off the end\n");
    {
        /* What a wrongly-guessed family looks like: garbage in, but never a
         * read past n. */
        const uint8_t f[] = { 10, 1, 0x01, 35, 4, 0x00, 0x00, 0x1B, 0x5A };
        size_t i = 0; uint16_t id; uint8_t len; const uint8_t *v;
        int guard = 0;
        while (gopro_status_next(f, sizeof(f), true, &i, &id, &len, &v)) {
            CHECK(v + len <= f + sizeof(f), "every value stays inside the frame");
            if (++guard > 8) { CHECK(0, "walker did not terminate"); break; }
        }
        CHECK(i <= sizeof(f), "cursor never passes the end");
    }

    printf("5. setting replies in both forms\n");
    {
        uint16_t id; uint8_t st;
        const uint8_t narrow[] = { 0x5B, 0x00 };
        const uint8_t wide[]   = { 0xFF, 0x00, 0x5B, 0x02 };
        const uint8_t cut[]    = { 0xFF, 0x00 };
        CHECK(gopro_setting_reply(narrow, sizeof(narrow), &id, &st) && id == 0x5B && st == 0, "narrow keep-alive ok");
        CHECK(gopro_setting_reply(wide, sizeof(wide), &id, &st) && id == 0x5B && st == 2, "wide keep-alive refused");
        CHECK(!gopro_setting_reply(cut, sizeof(cut), &id, &st), "a cut wide reply is not a reply");
    }

    printf("\n%s (%d failures)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
