/*
 * The preset guard: when the module refuses to run a GoPro's shutter.
 *
 * WHY THIS MATTERS ENOUGH TO TEST. GoPro's shutter fires whatever the current
 * preset does. In a photo preset that is a still, and the record state machine
 * re-issues an unmet record request every few seconds -- so arming in a photo
 * preset means a photograph every few seconds for a whole flight, filling the
 * card, while the OSD says the camera is rolling.
 *
 * The other half of the test is the half that is easy to get wrong: refusing
 * TOO MUCH. This firmware talks to cameras GoPro does not document, and a
 * guard that treats silence as "photo" would break recording on any camera
 * that does not report status 96.
 */
#include <stdio.h>
#include "gopro_cam.h"

static int fails = 0;
#define CHECK(cond, what) do { \
    if (!(cond)) { printf("  FAIL: %s\n", (what)); fails++; } } while (0)

int main(void)
{
    printf("GoPro preset guard\n\n");

    printf("1. a known stills preset is caught\n");
    CHECK(gopro_preset_takes_stills(GOPRO_PRESET_PHOTO, true), "photo takes stills");
    CHECK(gopro_preset_takes_stills(GOPRO_PRESET_TIMELAPSE, true), "timelapse takes stills");

    printf("2. video is not\n");
    CHECK(!gopro_preset_takes_stills(GOPRO_PRESET_VIDEO, true), "video films");

    printf("3. BLOCKER: an unread preset must never block recording\n");
    /* The camera has not answered, or does not report status 96 at all. Every
     * value is meaningless while `valid` is false, including the ones that
     * would otherwise match. */
    CHECK(!gopro_preset_takes_stills(GOPRO_PRESET_PHOTO, false), "photo id, but not valid");
    CHECK(!gopro_preset_takes_stills(GOPRO_PRESET_TIMELAPSE, false), "timelapse id, but not valid");
    CHECK(!gopro_preset_takes_stills(0, false), "zeroed and not valid");

    printf("4. BLOCKER: a group we have never heard of must not block either\n");
    /* GoPro can invent a preset group tomorrow. Refusing on an unknown value
     * would turn a camera that films perfectly well into one that will not
     * start, which is a worse failure than the one being guarded against. */
    CHECK(!gopro_preset_takes_stills(1003, true), "a future group films");
    CHECK(!gopro_preset_takes_stills(0, true), "group 0 films");
    CHECK(!gopro_preset_takes_stills(0xFFFFFFFFu, true), "nonsense films");

    printf("5. status 97 -> group, on the values a HERO (2024) actually sent\n");
    /* Recorded 2026-09-07 while the dial was turned: video presets pushed
     * 0, 3 and 5; the photo preset pushed 0x10000. Status 96 never moved. */
    CHECK(gopro_preset_group_from_id(0x00010000u) == GOPRO_PRESET_PHOTO, "0x10000 is photo");
    CHECK(gopro_preset_takes_stills(gopro_preset_group_from_id(0x00010000u), true), "...and takes stills");
    CHECK(gopro_preset_group_from_id(0) == GOPRO_PRESET_VIDEO, "0 is video");
    CHECK(gopro_preset_group_from_id(3) == GOPRO_PRESET_VIDEO, "3 is video");
    CHECK(gopro_preset_group_from_id(5) == GOPRO_PRESET_VIDEO, "5 is video");
    CHECK(!gopro_preset_takes_stills(gopro_preset_group_from_id(5), true), "...and films");
    CHECK(gopro_preset_group_from_id(0x00020001u) == GOPRO_PRESET_TIMELAPSE, "0x2xxxx is timelapse");
    CHECK(gopro_preset_takes_stills(gopro_preset_group_from_id(0x00020001u), true), "...and takes stills");
    /* An index GoPro has not used yet lands on an unknown group and films. */
    CHECK(!gopro_preset_takes_stills(gopro_preset_group_from_id(0x00030000u), true), "0x3xxxx films");

    printf("\n%s (%d failures)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
