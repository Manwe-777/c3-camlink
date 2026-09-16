/*
 * camvendor: advertisement parsing, model naming, and the bind POLICY.
 *
 * The policy tests are the point of this file. There is deliberately no
 * bind-time model gate: the two GoPros this firmware is tested against are a
 * HERO (2024) and a HERO5 Black, and any gate drawn on GoPro's documented list
 * would refuse both of them. If a gate is ever added, these tests are what
 * says no.
 */
#include <stdio.h>
#include <string.h>
#include "camvendor.h"

static int fails = 0;
#define CHECK(cond, what) do { \
    if (!(cond)) { printf("  FAIL: %s\n", (what)); fails++; } } while (0)

/* Build an AD blob from {len,type,payload...} runs. */
static uint8_t adv[64];
static uint8_t adv_len;
static void adv_reset(void) { adv_len = 0; memset(adv, 0, sizeof(adv)); }
static void adv_add(uint8_t type, const uint8_t *data, uint8_t n)
{
    adv[adv_len++] = (uint8_t)(n + 1);
    adv[adv_len++] = type;
    memcpy(&adv[adv_len], data, n);
    adv_len = (uint8_t)(adv_len + n);
}

/* A GoPro manufacturer field: company id, schema, status, model, caps. */
static void adv_add_gopro_mfg(uint8_t status, uint8_t model)
{
    const uint8_t d[6] = { GOPRO_COMPANY_ID & 0xFF, GOPRO_COMPANY_ID >> 8,
                           0x01, status, model, 0x00 };
    adv_add(0xFF, d, sizeof(d));
}
static void adv_add_gopro_svc(void)
{
    const uint8_t u[2] = { GOPRO_SERVICE_UUID16 & 0xFF, GOPRO_SERVICE_UUID16 >> 8 };
    adv_add(0x03, u, sizeof(u));
}

int main(void)
{
    printf("camvendor tests\n\n");

    printf("1. the vendor enum leaves 0 meaning \"no camera\"\n");
    {
        /* Load-bearing: a zeroed scan entry must not read as a DJI camera.
         * Renumbering this is only safe while the vendor byte stays out of
         * NVS -- what is persisted is cam_proto_t. */
        CHECK(CAM_VENDOR_NONE == 0, "NONE is 0");
        CHECK(CAM_VENDOR_DJI != 0 && CAM_VENDOR_GOPRO != 0, "real vendors are non-zero");
        CHECK(CAM_VENDOR_DJI != CAM_VENDOR_GOPRO, "vendors are distinct");
    }

    printf("2. a GoPro is recognised by its service UUID\n");
    {
        adv_reset(); adv_add_gopro_svc();
        CHECK(cam_adv_is_gopro(adv, adv_len), "service UUID alone identifies a GoPro");
    }

    printf("3. ...and by its company id alone, which is proof enough\n");
    {
        /* The HERO5 arm of the policy. An advert carrying only manufacturer
         * data still identifies the camera, and dropping this would hide
         * hardware that works. */
        adv_reset(); adv_add_gopro_mfg(0x01, 0);
        CHECK(cam_adv_is_gopro(adv, adv_len), "company id alone identifies a GoPro");
    }

    printf("4. ...in either byte order\n");
    {
        adv_reset();
        const uint8_t d[6] = { GOPRO_COMPANY_ID >> 8, GOPRO_COMPANY_ID & 0xFF,
                               0x01, 0x01, GOPRO_MODEL_HERO12, 0x00 };
        adv_add(0xFF, d, sizeof(d));
        CHECK(cam_adv_is_gopro(adv, adv_len), "byte-swapped company id still matches");
    }

    printf("5. a DJI advert is not a GoPro\n");
    {
        adv_reset();
        const uint8_t d[8] = { 0xAA, 0x08, 0x03, 0x11, 0x22, 0x33, 0x44, 0x55 };
        adv_add(0xFF, d, sizeof(d));
        CHECK(!cam_adv_is_gopro(adv, adv_len), "a DJI manufacturer field is not GoPro");
        CHECK(cam_adv_gopro_model(adv, adv_len) == 0,
              "and its fifth byte is not read as a model");
    }

    printf("6. the model comes out of the advert at the right offset\n");
    {
        adv_reset(); adv_add_gopro_svc(); adv_add_gopro_mfg(0x01, GOPRO_MODEL_HERO2024);
        CHECK(cam_adv_gopro_model(adv, adv_len) == GOPRO_MODEL_HERO2024,
              "model byte read past the company id, not over it");
    }

    printf("7. awake is answered only when it can be\n");
    {
        bool known = true;
        adv_reset(); adv_add_gopro_svc();
        CHECK(!cam_adv_gopro_awake(adv, adv_len, &known) && !known,
              "no manufacturer data -> not known, not \"asleep\"");

        adv_reset(); adv_add_gopro_svc(); adv_add_gopro_mfg(0x01, GOPRO_MODEL_HERO13);
        CHECK(cam_adv_gopro_awake(adv, adv_len, &known) && known, "awake bit set");

        adv_reset(); adv_add_gopro_svc(); adv_add_gopro_mfg(0x00, GOPRO_MODEL_HERO13);
        CHECK(!cam_adv_gopro_awake(adv, adv_len, &known) && known, "awake bit clear");
    }

    printf("8. a truncated or malformed advert is survived, not walked off\n");
    {
        CHECK(cam_adv_field(NULL, 0, 0xFF, NULL) == NULL, "NULL advert");
        CHECK(!cam_adv_is_gopro(NULL, 0), "NULL is not a GoPro");

        const uint8_t bad[4] = { 0x20, 0xFF, 0x01, 0x02 };  /* len runs past end */
        CHECK(cam_adv_field(bad, sizeof(bad), 0xFF, NULL) == NULL,
              "a length past the end stops the walk");

        const uint8_t zero[4] = { 0x00, 0x00, 0x00, 0x00 }; /* zero length */
        CHECK(cam_adv_field(zero, sizeof(zero), 0xFF, NULL) == NULL,
              "a zero length stops the walk rather than spinning");
    }

    printf("9. POLICY: the cameras this firmware is tested against may bind\n");
    {
        /* A model gate would refuse both of these -- one by name, one by
         * being older than the HERO 9 floor. If either of these lines ever
         * fails, such a gate has been added and the firmware can no longer
         * talk to the only GoPros it has been tested against. */
        CHECK(cam_gopro_model_note(GOPRO_MODEL_HERO2024) != NULL,
              "HERO (2024) is annotated");
        CHECK(cam_gopro_model_note(50) != NULL, "a pre-HERO9 model is annotated");
        CHECK(cam_gopro_model_note(0) != NULL, "an unreadable model is annotated");

        /* The note exists to be SHOWN, never to refuse. There is no predicate
         * in this header that a caller could use as a gate. */
        CHECK(cam_gopro_model_note(GOPRO_MODEL_HERO13) == NULL,
              "a listed model has nothing to say");
        CHECK(cam_gopro_model_note(GOPRO_MODEL_MISSION1) == NULL, "MISSION 1 is quiet");
        CHECK(cam_gopro_model_note(99) == NULL, "a future model is quiet");
    }

    printf("10. the 2-byte id family is picked for the MISSION line and newer\n");
    {
        CHECK(cam_gopro_wide_ids(GOPRO_MODEL_MISSION1), "MISSION 1 is wide");
        CHECK(cam_gopro_wide_ids(GOPRO_MODEL_MISSION1PRO), "MISSION 1 Pro is wide");
        CHECK(cam_gopro_wide_ids(72), "newer than any we know is assumed wide");
        CHECK(!cam_gopro_wide_ids(GOPRO_MODEL_HERO13), "HERO13 is narrow");
        CHECK(!cam_gopro_wide_ids(GOPRO_MODEL_HERO2024), "HERO (2024) is narrow");
        CHECK(!cam_gopro_wide_ids(GOPRO_MODEL_MAX2), "MAX 2 is narrow");
    }

    printf("11. OSD labels fit the field and name the generation\n");
    {
        char l[6];
        cam_gopro_label(GOPRO_MODEL_HERO12, l);   CHECK(!strcmp(l, "GP12"), "GP12");
        cam_gopro_label(GOPRO_MODEL_HERO13, l);   CHECK(!strcmp(l, "GP13"), "GP13");
        cam_gopro_label(GOPRO_MODEL_HERO2024, l); CHECK(!strcmp(l, "GP24"), "GP24");
        cam_gopro_label(GOPRO_MODEL_MISSION1, l); CHECK(!strcmp(l, "MSN"),  "MSN");
        cam_gopro_label(0, l);                    CHECK(!strcmp(l, "GPRO"), "unknown -> GPRO");

        /* Every label in the table has to fit the five bytes plus a NUL that
         * the OSD field allows. */
        for (int m = 0; m < 256; m++) {
            char t[6]; memset(t, 0x7F, sizeof(t));
            cam_gopro_label((uint8_t)m, t);
            CHECK(strlen(t) <= 5, "every label fits");
        }
    }

    printf("12. every model in the table has a name\n");
    {
        CHECK(!strcmp(cam_gopro_model_name(GOPRO_MODEL_HERO2024), "GoPro HERO (2024)"),
              "the HERO (2024) is named, not just tolerated");
        CHECK(!strcmp(cam_gopro_model_name(0), "GoPro"), "unknown falls back");
    }

    printf("\n%s (%d failures)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
