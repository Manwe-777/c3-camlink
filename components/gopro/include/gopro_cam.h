/*
 * GoPro camera session over BLE, per the Open GoPro specification:
 * connect, bond, subscribe, keep alive, track status, control the shutter.
 *
 * Bring-up target is a HERO (2024), which Open GoPro does not list as
 * supported. Everything here is therefore written to SAY what the camera
 * answered rather than assume it: every command's response status is logged,
 * and a status registration the camera refuses falls back to asking for
 * everything and reading what arrives.
 */
#ifndef CAMLINK_GOPRO_CAM_H
#define CAMLINK_GOPRO_CAM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool     link_up;        /* connected, bonded, subscribed, keep-alive running */
    bool     status_valid;   /* at least one status value has arrived this link  */
    uint32_t status_age_ms;  /* since the last status push, UINT32_MAX if never   */

    bool     encoding;       /* status 10                                         */
    uint32_t clip_s;         /* status 13, video encoding duration                */
    uint32_t left_s;         /* status 35, remaining video time                   */
    uint8_t  battery_pct;    /* status 70                                         */
    bool     battery_valid;
    uint8_t  hot;            /* status 6, overheating                             */
    bool     busy;           /* status 8                                          */
    bool     sd_ok;          /* status 33 == 0 (OK)                               */

    /* Which preset group the camera is in. This decides whether the shutter
     * would film or photograph -- see gopro_preset_takes_stills().
     *
     * Derived from status 97 (the active preset id, whose upper 16 bits are
     * the group) when the camera reports it, and from status 96 (the preset
     * group itself) only until it does. The HERO (2024) reports 96 = 1000
     * (VIDEO) while sitting on a photo preset -- measured on the bench,
     * 2026-09-07 -- while 97 moved 0/3/5 <-> 0x10000 with the dial. See
     * gopro_preset_group_from_id(). */
    uint32_t preset_group;
    bool     preset_valid;
    uint32_t preset_id;      /* status 97 raw, meaningful when preset_id_valid   */
    bool     preset_id_valid;
    bool     can_record;     /* false only when we KNOW the shutter takes stills */

    bool     wide_ids;       /* status ids are 2 bytes (MISSION 1 family)      */

    char     res[8];         /* video resolution label ("4K", "1080"), "" unknown */
    char     fps[8];         /* framerate label ("60", "30"), "" unknown          */
    char     model[24];      /* from Get Hardware Info, "" until it answers       */
    char     label[6];       /* four-character OSD label derived from model       */
} gopro_status_t;

/* Preset groups, from status 96. Confirmed against GoPro's own SDK, which
 * names that status PRESET_GROUP. */
#define GOPRO_PRESET_VIDEO      1000
#define GOPRO_PRESET_PHOTO      1001
#define GOPRO_PRESET_TIMELAPSE  1002

/* The group encoded in a preset id (status 97). Open GoPro numbers presets
 * as <group index> << 16 | <index within the group>: video presets are
 * 0x0000xxxx, photo 0x0001xxxx, timelapse 0x0002xxxx. Mapping the index onto
 * the 1000-series keeps one vocabulary for both sources, so the guard below
 * and every log line read the same whichever status the camera reported.
 *
 * WHY 97 AND NOT 96. On a HERO (2024) status 96 stays at 1000 (VIDEO) on a
 * photo preset, so a guard fed only by 96 let the shutter take a still, which
 * is the exact failure it exists to prevent. 97 tracked the dial every time.
 * Pure, so the host test runs the shipped mapping on the recorded values. */
static inline uint32_t gopro_preset_group_from_id(uint32_t preset_id)
{
    return GOPRO_PRESET_VIDEO + (preset_id >> 16);
}

/* Would the shutter take stills rather than film?
 *
 * WHY THIS EXISTS. GoPro's shutter command fires whatever the current preset
 * does. In a photo or timelapse preset that is a still -- and the record state
 * machine above re-issues an unmet record request every few seconds, so
 * arming would take a photograph every few seconds for the whole flight,
 * filling the card, while the pilot watches an OSD that says REC. That is
 * exactly the failure this firmware exists to prevent, arrived at from the
 * other direction.
 *
 * ONLY SAYS YES WHEN IT KNOWS. An unread preset, or a group id GoPro has not
 * invented yet, is not a reason to refuse: this firmware binds cameras that
 * are not on GoPro's list, and one of them may well answer this differently or
 * not at all. Refusing on silence would turn a camera that films perfectly well today
 * into one that will not start. So the default is to let the shutter run, and
 * the guard fires only on a group positively known to shoot stills.
 *
 * Pure, so the host tests exercise the shipped decision rather than a copy. */
static inline bool gopro_preset_takes_stills(uint32_t group, bool valid)
{
    if (!valid) return false;
    return group == GOPRO_PRESET_PHOTO || group == GOPRO_PRESET_TIMELAPSE;
}

/* GoPro's two status-id families.
 *
 * The HERO9 through HERO13, MAX 2 and LIT HERO carry status ids as one byte
 * under query ids 0x13/0x53/0x93. The MISSION 1 and MISSION 1 Pro carry them
 * as two bytes, big-endian, under their own ids 0x16/0x56/0x96, and the
 * results array is [id hi][id lo][len][value] in both the reply and the push.
 * A firmware that only spoke the old family would connect to a MISSION 1,
 * drive its shutter perfectly, and never learn whether it was recording.
 * Which family to try first comes from cam_gopro_wide_ids(model); the camera's
 * reply has the final say. */
#define GOPRO_Q_GET_STATUS      0x13
#define GOPRO_Q_REG_STATUS      0x53
#define GOPRO_Q_PUSH_STATUS     0x93
#define GOPRO_Q_GET_STATUS16    0x16
#define GOPRO_Q_REG_STATUS16    0x56
#define GOPRO_Q_PUSH_STATUS16   0x96
#define GOPRO_SET_WIDE_MARKER   0xFF   /* a setting write/reply in the wide form */

/* Walk one element of a status results array in either family.
 *
 * *i is the cursor; on success it advances past the element and id/len/val
 * describe it. False at the end of the array OR at a truncated element, so a
 * short frame stops the walk rather than reading past it. Pure, so the host
 * test runs the shipped walker on both families and on a cut-off frame. */
static inline bool gopro_status_next(const uint8_t *p, size_t n, bool wide,
                                     size_t *i, uint16_t *id, uint8_t *len,
                                     const uint8_t **val)
{
    size_t hdr = wide ? 3 : 2;
    if (p == NULL || *i + hdr > n) return false;
    uint16_t e_id  = wide ? (uint16_t)(((uint16_t)p[*i] << 8) | p[*i + 1]) : p[*i];
    uint8_t  e_len = p[*i + hdr - 1];
    if (*i + hdr + e_len > n) return false;
    if (id)  *id  = e_id;
    if (len) *len = e_len;
    if (val) *val = p + *i + hdr;
    *i += hdr + e_len;
    return true;
}

/* The id and status of a setting reply in either form: the wide one is
 * [0xFF][id hi][id lo][status], the narrow one [id][status]. */
static inline bool gopro_setting_reply(const uint8_t *m, size_t n,
                                       uint16_t *id, uint8_t *status)
{
    if (m == NULL || n < 2) return false;
    if (m[0] == GOPRO_SET_WIDE_MARKER) {
        if (n < 4) return false;
        if (id)     *id = (uint16_t)(((uint16_t)m[1] << 8) | m[2]);
        if (status) *status = m[3];
        return true;
    }
    if (id)     *id = m[0];
    if (status) *status = m[1];
    return true;
}

/* Which characteristic a raw write goes to. */
typedef enum { GOPRO_CH_CMD = 0, GOPRO_CH_SET = 1, GOPRO_CH_QRY = 2 } gopro_ch_t;

/* Declare the GoPro GATT layout to the shared transport. Called once at
 * start-up, before anything connects.
 *
 * REGISTERED RATHER THAN SET, because of an ordering problem: discovery runs
 * as part of connecting, but which camera won the scan is only decided inside
 * that same call. There is no moment between "we know it is a GoPro" and "we
 * must know which characteristics to look for" in which a caller could set it.
 *
 * Selecting is deliberately all this does. It brings up no BLE controller of
 * its own -- one stack, owned by the transport, is what lets the module switch
 * between saved cameras of different vendors without rebooting. */
void gopro_cam_select(void);

/* Bring the protocol up on a link the transport has already made and bonded.
 * False means the camera is not answering -- drop the link and retry.
 *
 * `model` is the advertised model id, or 0 when it could not be read; it only
 * chooses which status-id family to try first, and the camera's own answer
 * decides. An unknown model therefore costs a round trip and nothing else,
 * which matters in a fork that binds cameras GoPro does not list. */
bool gopro_cam_session_start(uint8_t model);

/* One pass of the session. Called by the camera task while the link is up. */
void gopro_cam_tick(void);

/* The link has gone. Forgets everything the camera said, so a stale overheat
 * or full card cannot follow the module into its next connection. */
void gopro_cam_session_end(void);

/* Every notification the transport receives while a GoPro is connected. */
void gopro_cam_on_notify(uint16_t handle, const uint8_t *data, size_t len);

/* False after a sleep, until gopro_cam_wake(). The camera task uses it to stop
 * reaching for a camera it has just deliberately put down. */
bool gopro_cam_wants_camera(void);

void gopro_cam_get(gopro_status_t *out);

/* Set Shutter. Executed by the session task on its next pass. */
void gopro_cam_request_record(bool on);

/* Put the camera to sleep. It stays connectable; reconnecting wakes it. */
void gopro_cam_sleep(void);

/* Tag a HiLight moment (command 0x18). Meaningful while recording. */
void gopro_cam_hilight(void);

/* Ask the camera to reconnect now (e.g. after sleep, to wake it). */
void gopro_cam_wake(void);

/* Drop the link politely and stop reconnecting. Called before a reboot: a
 * link that just vanishes leaves the camera waiting out its supervision
 * timeout, during which it neither advertises nor accepts a connection --
 * which is exactly the window the next boot wants to find it in. */
void gopro_cam_disconnect(void);

/* Raw packet to one of the three write characteristics, bytes as they appear
 * in the Open GoPro tables, header included. Bench tool. Returns false if not
 * connected. */
bool gopro_cam_write(gopro_ch_t ch, const uint8_t *bytes, size_t n);

#endif
