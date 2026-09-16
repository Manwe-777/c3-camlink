/*
 * Which vendor a camera is, and what its advertisement says about it.
 *
 * WHY THIS IS ITS OWN COMPONENT. The BLE transport in components/dji_link/ has
 * to recognise a GoPro advertisement, and the GoPro session has to speak the
 * protocol. If the transport depended on the GoPro component and the GoPro
 * component depended on the transport, that is a build cycle. So the
 * vendor-facing FACTS -- how an advert identifies itself, which UUIDs a vendor
 * uses -- live here, in a leaf that depends on nothing in this tree, and both
 * sides depend on it.
 *
 * It also keeps GoPro constants out of components/dji_link/, which is vendored
 * MIT code tracked by NOTICE.
 *
 * Two decisions here are load-bearing enough to be marked C3CAMLINK POLICY
 * below: the vendor enum starts at CAM_VENDOR_NONE, and there is no bind-time
 * model refusal.
 */
#ifndef CAMVENDOR_H
#define CAMVENDOR_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* C3CAMLINK POLICY: NONE is 0, so a zeroed struct means "no camera" rather
 * than the first real vendor. That is safe ONLY because the vendor byte is
 * never persisted: what goes into NVS is cam_proto_t, resolved from the vendor
 * at bind time and stored in cam_profile_t.proto. Check that again before
 * adding a vendor. */
typedef enum {
    CAM_VENDOR_NONE = 0,   /* not a camera we know                          */
    CAM_VENDOR_DJI,
    CAM_VENDOR_GOPRO,
} cam_vendor_t;

/* ==========================================================================
 * GoPro advertisements
 *
 * A GoPro advertises the SIG-assigned 16-bit service 0xFEA6 and, separately,
 * manufacturer data under its company id that carries a status bitfield and
 * the model id.
 * ========================================================================== */

#define GOPRO_SERVICE_UUID16   0xFEA6

/* GoPro's SIG company identifier.
 *
 * C3CAMLINK POLICY: the company id is accepted as an ALTERNATIVE proof of
 * vendor, not merely recorded. The cameras this firmware is tested against are
 * a HERO (2024) and a HERO5 Black, and the reason it can talk to them is that
 * it identifies a camera by anything that identifies it rather than by one
 * blessed field. A camera found only by its company id is still a camera;
 * refusing it would hide hardware that works.
 *
 * Written 0x02F2 here and matched in EITHER byte order, because the Open GoPro
 * text writes it as "0xF202" and a firmware that takes that literally would
 * otherwise go unrecognised. Matching both costs one comparison. */
#define GOPRO_COMPANY_ID       0x02F2

/* Model ids, from Get Hardware Info and from the advertisement's model byte,
 * checked against GoPro's own firmware table (HERO9 55 ... MISSION 1 71).
 *
 * The table is kept for what it can NAME, not for what it can refuse. Knowing
 * the model before connecting is what lets the picker say "GoPro HERO (2024)"
 * instead of "GoPro", and what gives the OSD a GP12/GP13/MSN label. */
#define GOPRO_MODEL_HERO9        55
#define GOPRO_MODEL_HERO10       57
#define GOPRO_MODEL_HERO11       58
#define GOPRO_MODEL_HERO11_MINI  60
#define GOPRO_MODEL_HERO12       62
#define GOPRO_MODEL_MAX2         64
#define GOPRO_MODEL_HERO13       65
#define GOPRO_MODEL_HERO2024     66   /* "HERO" (2024), firmware H24.03    */
#define GOPRO_MODEL_MISSION1PRO  69
#define GOPRO_MODEL_LITHERO      70
#define GOPRO_MODEL_MISSION1     71

/* The oldest model Open GoPro documents. NOT a floor: see cam_gopro_model_note().
 * Kept because "older than anything GoPro documents" is worth SAYING. */
#define GOPRO_MODEL_OPENGOPRO_MIN  GOPRO_MODEL_HERO9

/* Walk AD structures. Returns the data pointer for `want_type` and writes its
 * length, or NULL. Shared by every parser below. */
static inline const uint8_t *cam_adv_field(const uint8_t *adv, uint8_t adv_len,
                                           uint8_t want_type, uint8_t *out_len)
{
    if (adv == NULL) return NULL;
    for (uint8_t i = 0; i + 1 < adv_len; ) {
        uint8_t len = adv[i];
        if (len == 0 || (uint16_t)(i + len + 1) > adv_len) break;
        uint8_t type = adv[i + 1];
        if (type == want_type) {
            if (out_len) *out_len = (uint8_t)(len - 1);
            return &adv[i + 2];
        }
        i = (uint8_t)(i + len + 1);
    }
    return NULL;
}

/* Does this advertisement carry the GoPro service UUID?
 *
 * Checked in both the complete (0x03) and incomplete (0x02) 16-bit service
 * lists, little-endian, because which one a camera uses is not something to
 * bet a scan filter on. */
static inline bool cam_adv_gopro_has_service(const uint8_t *adv, uint8_t adv_len)
{
    const uint8_t types[2] = { 0x02, 0x03 };
    for (int t = 0; t < 2; t++) {
        uint8_t n = 0;
        const uint8_t *d = cam_adv_field(adv, adv_len, types[t], &n);
        if (d == NULL) continue;
        for (uint8_t i = 0; i + 1 < n; i += 2) {
            uint16_t u = (uint16_t)(d[i] | ((uint16_t)d[i + 1] << 8));
            if (u == GOPRO_SERVICE_UUID16) return true;
        }
    }
    return false;
}

/* Does the manufacturer field carry GoPro's company id, in either byte order? */
static inline bool cam_adv_gopro_has_company(const uint8_t *adv, uint8_t adv_len)
{
    uint8_t n = 0;
    const uint8_t *d = cam_adv_field(adv, adv_len, 0xFF, &n);
    if (d == NULL || n < 2) return false;
    return (d[0] == (GOPRO_COMPANY_ID & 0xFF) && d[1] == (GOPRO_COMPANY_ID >> 8)) ||
           (d[1] == (GOPRO_COMPANY_ID & 0xFF) && d[0] == (GOPRO_COMPANY_ID >> 8));
}

/* Is this a GoPro? Either proof is enough -- see GOPRO_COMPANY_ID. */
static inline bool cam_adv_is_gopro(const uint8_t *adv, uint8_t adv_len)
{
    return cam_adv_gopro_has_service(adv, adv_len) ||
           cam_adv_gopro_has_company(adv, adv_len);
}

/* GoPro's manufacturer-specific data, counted from the start of the AD
 * structure's payload -- which is where cam_adv_field() points, and which
 * INCLUDES the two company-id bytes:
 *
 *   0-1  company id
 *   2    schema version
 *   3    camera status   (bit 0 = awake)
 *   4    model id
 *   5    capabilities
 *
 * Confirmed against GoPro's own Kotlin SDK, whose parser reads schema at [0],
 * status at [1] and cameraId at [2] -- it is handed the payload with the
 * company id already stripped by the phone's BLE API, which ESP-IDF does not
 * do. Getting that difference wrong shifts everything by two, and reading the
 * table as absolute offsets into the advert shifts it by seven; both land on
 * bytes that carry a serial number and look like plausible rubbish. */
#define GOPRO_ADV_SCHEMA_OFFSET  2
#define GOPRO_ADV_STATUS_OFFSET  3
#define GOPRO_ADV_MODEL_OFFSET   4

/* The model id, or 0 when it cannot be read.
 *
 * Zero means "unknown", and unknown is a thing you are allowed to bind -- it
 * is what a GoPro older than the table covers looks like. The caller must treat
 * it as "no name to show", not as a reason to refuse. */
static inline uint8_t cam_adv_gopro_model(const uint8_t *adv, uint8_t adv_len)
{
    /* Guarded on the vendor first: a DJI advert's manufacturer field is
     * perfectly well-formed and its fifth byte is a number -- it just is not a
     * GoPro model. Something has to establish the vendor before these offsets
     * mean anything. */
    if (!cam_adv_is_gopro(adv, adv_len)) return 0;

    uint8_t n = 0;
    const uint8_t *d = cam_adv_field(adv, adv_len, 0xFF, &n);   /* mfg data */
    if (d == NULL || n <= GOPRO_ADV_MODEL_OFFSET) return 0;
    return d[GOPRO_ADV_MODEL_OFFSET];
}

/* Is the camera awake, as far as its advertisement admits?
 *
 * The low bit of the status byte is the processor state. `*known` says whether
 * the question could be answered at all -- an advert without manufacturer data
 * tells us nothing, and "I cannot tell" must never read as "asleep", because
 * that would demote a camera sitting there switched on. */
#define GOPRO_ADV_STATUS_AWAKE   0x01

static inline bool cam_adv_gopro_awake(const uint8_t *adv, uint8_t adv_len,
                                       bool *known)
{
    if (known) *known = false;
    if (!cam_adv_is_gopro(adv, adv_len)) return false;
    uint8_t n = 0;
    const uint8_t *d = cam_adv_field(adv, adv_len, 0xFF, &n);
    if (d == NULL || n <= GOPRO_ADV_STATUS_OFFSET) return false;
    if (known) *known = true;
    return (d[GOPRO_ADV_STATUS_OFFSET] & GOPRO_ADV_STATUS_AWAKE) != 0;
}

/* What is worth SAYING about this model, or NULL when there is nothing to say.
 *
 * C3CAMLINK POLICY: THIS IS ADVISORY. IT NEVER REFUSES A BIND.
 *
 * The tempting thing to write here instead is a gate: refuse anything GoPro
 * does not document, so that every camera in the user's list is promised to
 * connect. That trade is refused deliberately. Model 66 is the HERO (2024)
 * verified recording on hardware here, and the other test camera is a HERO5
 * Black, far below the HERO 9 where GoPro's own documentation starts. A gate
 * drawn on GoPro's list would refuse both of the only two GoPros this firmware
 * has ever been tested against.
 *
 * So the rule is: say what is known, and let the camera answer for itself. The
 * cost of binding a camera that turns out not to work is a note on the settings
 * page; the cost of the gate is a user who cannot use hardware they own.
 *
 * Deliberately NOT named cam_gopro_model_supported(): a predicate with that
 * name and the opposite meaning is a trap for whoever reads this next. */
static inline const char *cam_gopro_model_note(uint8_t model)
{
    if (model == 0)
        return "this GoPro did not say which model it is -- it may still work";
    if (model == GOPRO_MODEL_HERO2024)
        return "not on GoPro's supported list; verified working here";
    if (model == GOPRO_MODEL_LITHERO)
        return "not on GoPro's supported list; untested here";
    if (model < GOPRO_MODEL_OPENGOPRO_MIN)
        return "older than a HERO 9, which is where GoPro's documentation starts";
    return NULL;
}

/* Does this model carry setting and status ids as two bytes?
 *
 * GoPro's Data Protocol page: cameras that report supports_2byte_ids use a
 * separate family of query ids with 2-byte element ids, and their operation
 * table lists that family for the MISSION 1 and MISSION 1 Pro only -- and
 * lists the 1-byte family for everything older and NOT for them. A model newer
 * than the newest we know is assumed to follow the MISSION 1, which is GoPro's
 * direction of travel; either way the session confirms the choice from the
 * camera's own reply and switches if refused, so this is a first guess rather
 * than a verdict. */
static inline bool cam_gopro_wide_ids(uint8_t model)
{
    return model == GOPRO_MODEL_MISSION1 || model == GOPRO_MODEL_MISSION1PRO ||
           model > GOPRO_MODEL_MISSION1;
}

/* Four characters for the OSD, matching what the DJI side does. */
static inline void cam_gopro_label(uint8_t model, char out[6])
{
    const char *s;
    switch (model) {
    case GOPRO_MODEL_HERO9:       s = "GP9";  break;
    case GOPRO_MODEL_HERO10:      s = "GP10"; break;
    case GOPRO_MODEL_HERO11:      s = "GP11"; break;
    case GOPRO_MODEL_HERO11_MINI: s = "GP11"; break;
    case GOPRO_MODEL_HERO12:      s = "GP12"; break;
    case GOPRO_MODEL_HERO13:      s = "GP13"; break;
    case GOPRO_MODEL_MAX2:        s = "MAX2"; break;
    case GOPRO_MODEL_HERO2024:    s = "GP24"; break;
    case GOPRO_MODEL_MISSION1:
    case GOPRO_MODEL_MISSION1PRO: s = "MSN";  break;
    default:                      s = "GPRO"; break;
    }
    size_t i = 0;
    for (; s[i] && i < 5; i++) out[i] = s[i];
    out[i] = '\0';
}

/* Human-readable, for the settings page's camera list. */
static inline const char *cam_gopro_model_name(uint8_t model)
{
    switch (model) {
    case GOPRO_MODEL_HERO9:       return "GoPro HERO9 Black";
    case GOPRO_MODEL_HERO10:      return "GoPro HERO10 Black";
    case GOPRO_MODEL_HERO11:      return "GoPro HERO11 Black";
    case GOPRO_MODEL_HERO11_MINI: return "GoPro HERO11 Black Mini";
    case GOPRO_MODEL_HERO12:      return "GoPro HERO12 Black";
    case GOPRO_MODEL_MAX2:        return "GoPro MAX 2";
    case GOPRO_MODEL_HERO13:      return "GoPro HERO13 Black";
    case GOPRO_MODEL_HERO2024:    return "GoPro HERO (2024)";
    case GOPRO_MODEL_MISSION1PRO: return "GoPro Mission 1 Pro";
    case GOPRO_MODEL_LITHERO:     return "GoPro LIT HERO";
    case GOPRO_MODEL_MISSION1:    return "GoPro Mission 1";
    default:                      return "GoPro";
    }
}

#endif /* CAMVENDOR_H */
