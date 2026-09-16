/*
 * The warning ladder: which warning wins, and when the module stays quiet.
 *
 * Tests camlink_warn_pick() DIRECTLY, not through the renderer. That is what
 * makes the most important case testable at all -- "we asked for a recording
 * and there is not one" needs the record intent and a grace timer, neither of
 * which the renderer can see.
 *
 * The last section does go through the renderer, to check the verdict actually
 * reaches the screen.
 */
#include <stdio.h>
#include <string.h>
#include "camlink.h"
#include "camlink_warn.h"
#include "osd_fields.h"

static int fails = 0;
#define CHECK(cond, what) do { \
    if (!(cond)) { printf("  FAIL: %s\n", (what)); fails++; } } while (0)

/* A camera that is connected, rolling, and healthy on every axis. Each test
 * breaks exactly one thing, so what it is testing is what it changed. */
static camlink_warn_in_t ok(void)
{
    camlink_warn_in_t in = {0};
    in.want_record   = true;
    in.rec_valid     = true;
    in.recording     = true;
    in.temp_valid    = true;  in.temp_over = 0;
    in.card_valid    = true;  in.card_s    = 3600;
    in.batt_valid    = true;  in.batt_pct  = 80;
    in.warn_batt_pct = CAMLINK_WARN_BATT_PCT_DEFAULT;
    in.warn_card_s   = CAMLINK_WARN_CARD_S_DEFAULT;
    return in;
}

int main(void)
{
    printf("Warning ladder\n\n");

    printf("1. a healthy camera is quiet\n");
    { camlink_warn_in_t in = ok();
      CHECK(camlink_warn_pick(&in) == CAM_WARN_NONE, "nothing wrong"); }

    printf("2. no camera is a state, not a warning\n");
    /* The state field already reads NO CAM. A warning on top would replace the
     * one piece of information that explains every blank field beside it. */
    { camlink_warn_in_t in = ok(); in.cam_gone = true;
      in.card_fault = true; in.batt_pct = 1; in.temp_over = 3;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_NONE,
            "everything wrong, but there is no camera to say it about"); }

    printf("3. BLOCKER: REC? waits out the shutter delay\n");
    /* Arm-to-recording is a BLE round trip plus the camera's own shutter
     * delay. Warning inside that window put REC? on screen at EVERY arm, and a
     * warning that fires every time is one people stop reading. */
    { camlink_warn_in_t in = ok(); in.recording = false; in.want_unmet_long = false;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_NONE, "still within the grace period"); }
    { camlink_warn_in_t in = ok(); in.recording = false; in.want_unmet_long = true;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_NOT_REC, "past it, so it is a fault"); }

    printf("4. BLOCKER: a camera that has gone quiet is not one that refused\n");
    /* Reporting a refusal we cannot actually see would be inventing it. */
    { camlink_warn_in_t in = ok(); in.recording = false; in.want_unmet_long = true;
      in.rec_valid = false;
      CHECK(camlink_warn_pick(&in) != CAM_WARN_NOT_REC, "no recording state, no verdict"); }

    printf("5. not wanting a recording is never a fault\n");
    { camlink_warn_in_t in = ok(); in.want_record = false;
      in.recording = false; in.want_unmet_long = true;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_NONE, "nobody asked it to record"); }

    printf("6. no card outranks everything else that is wrong\n");
    /* It also MAKES not-recording true, and it says what to do about it. */
    { camlink_warn_in_t in = ok(); in.card_fault = true;
      in.recording = false; in.want_unmet_long = true; in.temp_over = 3;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_NO_SD, "card fault wins"); }

    printf("7. ...and a full card is reported even with no time reading\n");
    /* A camera with no card has nothing to say about time remaining, so a
     * check that leaned on card_s would go quiet exactly when it matters. */
    { camlink_warn_in_t in = ok(); in.card_fault = true; in.card_valid = false;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_NO_SD, "fault needs no time reading"); }
    { camlink_warn_in_t in = ok(); in.card_s = 0;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_NO_SD, "zero seconds left is also NO SD"); }

    printf("8. a photo preset outranks REC?, because it says why\n");
    { camlink_warn_in_t in = ok(); in.stills_mode = true;
      in.recording = false; in.want_unmet_long = true;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_STILLS, "PHOTO, not REC?"); }

    printf("9. too hot outranks REC? and a low battery\n");
    { camlink_warn_in_t in = ok(); in.temp_over = 2;
      in.recording = false; in.want_unmet_long = true; in.batt_pct = 5;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_HOT, "heat wins"); }

    printf("10. the camera's own early heat warning is not ours to raise\n");
    /* 1 means the camera is warm and still recording. Only 2 and 3 stop it. */
    { camlink_warn_in_t in = ok(); in.temp_over = 1;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_NONE, "temp_over 1 still records"); }

    printf("11. REC? outranks the two \"not yet\" warnings\n");
    { camlink_warn_in_t in = ok(); in.recording = false; in.want_unmet_long = true;
      in.batt_pct = 5; in.card_s = 30;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_NOT_REC, "happening now beats coming soon"); }

    printf("12. battery outranks card time\n");
    { camlink_warn_in_t in = ok(); in.batt_pct = 5; in.card_s = 30;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_BATT_LOW, "flat battery first"); }
    { camlink_warn_in_t in = ok(); in.card_s = 30;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_SD_LOW, "card alone says SD LOW"); }

    printf("13. BLOCKER: nothing is warned about unless the camera confirmed it\n");
    /* A warning invented from a value the camera never sent is an alarm with
     * nothing behind it, on the one screen the pilot is relying on. */
    { camlink_warn_in_t in = ok(); in.batt_valid = false; in.batt_pct = 0;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_NONE, "unconfirmed battery is silent"); }
    { camlink_warn_in_t in = ok(); in.card_valid = false; in.card_s = 0;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_NONE, "unconfirmed card is silent"); }
    { camlink_warn_in_t in = ok(); in.temp_valid = false; in.temp_over = 3;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_NONE, "unconfirmed heat is silent"); }

    printf("14. a zero threshold means \"do not warn me about this\"\n");
    { camlink_warn_in_t in = ok(); in.batt_pct = 1; in.warn_batt_pct = 0;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_NONE, "battery warning disabled"); }
    { camlink_warn_in_t in = ok(); in.card_s = 1; in.warn_card_s = 0;
      CHECK(camlink_warn_pick(&in) == CAM_WARN_NONE, "card warning disabled"); }

    printf("15. severity order is usable as a >= test\n");
    /* Load-bearing: NOT_REC and above all mean "no recording is happening",
     * which is what an escalating indicator keys on. */
    CHECK(CAM_WARN_NOT_REC > CAM_WARN_BATT_LOW && CAM_WARN_BATT_LOW > CAM_WARN_SD_LOW,
          "the two \"coming soon\" warnings sort below");
    CHECK(CAM_WARN_HOT > CAM_WARN_NOT_REC && CAM_WARN_STILLS > CAM_WARN_HOT &&
          CAM_WARN_NO_SD > CAM_WARN_STILLS, "and the rest sort above");

    printf("16. every warning has a word, and it fits the field\n");
    {
        const camlink_warn_t all[] = { CAM_WARN_SD_LOW, CAM_WARN_BATT_LOW,
                                       CAM_WARN_NOT_REC, CAM_WARN_HOT,
                                       CAM_WARN_STILLS, CAM_WARN_NO_SD };
        for (unsigned i = 0; i < sizeof(all)/sizeof(all[0]); i++) {
            const char *w = camlink_warn_word(all[i]);
            CHECK(w != NULL, "has a word");
            CHECK(w && strlen(w) <= (size_t)osd_fields[OSD_F_WARN].width,
                  "fits OSD_F_WARN without pushing the field beside it");
        }
        CHECK(camlink_warn_word(CAM_WARN_NONE) == NULL,
              "no warning renders blank and holds its place");
    }

    printf("17. the verdict reaches the screen\n");
    {
        static const uint8_t L[OSD_ROWS][OSD_ROW_FIELDS] = {
            { OSD_F_WARN, 0, 0, 0 }, { OSD_F_STATE, 0, 0, 0 },
            { 0, 0, 0, 0 }, { 0, 0, 0, 0 },
        };
        char out[4][17];
        cam_status_t c = {0};
        c.connected = true; c.recording_valid = true;
        c.warn = CAM_WARN_STILLS;
        camlink_format_osd(&c, true, true, 0, true, L, CAMLINK_SETUP_NONE, out);
        CHECK(strncmp(out[0], "PHOTO", 5) == 0, "the warning is rendered");

        c.warn = CAM_WARN_NONE;
        camlink_format_osd(&c, true, true, 0, true, L, CAMLINK_SETUP_NONE, out);
        CHECK(out[0][0] == '\0' || out[0][0] == ' ', "and a clear warning renders blank");
    }

    printf("\n%s (%d failures)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
