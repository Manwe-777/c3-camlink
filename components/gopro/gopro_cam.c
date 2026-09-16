/*
 * GoPro BLE session. See gopro_cam.h.
 *
 * Shape of the protocol (Open GoPro, BLE):
 *
 *   service 0xFEA6, six characteristics under the base UUID
 *   b5f9XXXX-aa8d-11e3-9046-0002a5d5c51b:
 *     0072 command       (write)     0073 command response (notify)
 *     0074 setting       (write)     0075 setting response (notify)
 *     0076 query         (write)     0077 query response   (notify)
 *
 *   Every message is packetised into <= 20-byte BLE writes. The first byte is
 *   a header: bit 7 set is a continuation packet; otherwise bits 6..5 say how
 *   the length is encoded (00: five bits in this byte; 01: thirteen bits over
 *   two; 10: sixteen bits over the next two). The bytes in the Open GoPro
 *   tables include that header, so "03 01 01 01" is sent exactly as written.
 *
 *   The camera must be BONDED. The first time, it has to be in pairing mode
 *   (Connections > Connect Device > Quik App); after that the bond is stored
 *   on both sides. It goes to sleep on its own unless a keep-alive arrives
 *   every few seconds, and while asleep it keeps advertising for hours, so a
 *   connection request is also how it is woken.
 */
#include "gopro_cam.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_defs.h"

#include "ble.h"   /* camlink_ble_apply_tx_level / camlink_ble_get_tx_level */
#include "camvendor.h"

static const char *TAG = "GOPRO";

#define GP_APP_ID        0x47
/* C3CAMLINK PATCH: was a local 0xFEA6. The scanner and this session must
 * agree on what a GoPro is, so the constant lives in camvendor now. */
#define GP_SVC_UUID16    GOPRO_SERVICE_UUID16

/* Characteristic slots. Order matters: the three responses are subscribed in
 * sequence, and the three writes are addressed by gopro_ch_t. */
enum { CH_CMD = 0, CH_SET, CH_QRY, CH_CMD_RSP, CH_SET_RSP, CH_QRY_RSP, CH_COUNT };
/* The slot names, for the logs. The UUIDs themselves live in s_gopro_profile
 * below, which is the single description the transport discovers from. */
static const char    *s_ch_name[CH_COUNT]  = { "cmd", "set", "qry", "cmd_rsp", "set_rsp", "qry_rsp" };

/* Command / setting / query IDs used here. */
#define CMD_SET_SHUTTER   0x01
#define CMD_SLEEP         0x05
#define CMD_HW_INFO       0x3C
#define SET_KEEP_ALIVE    0x5B
#define QRY_REG_STATUS    GOPRO_Q_REG_STATUS      /* 1-byte ids            */
#define QRY_STATUS_PUSH   GOPRO_Q_PUSH_STATUS
#define QRY_REG_STATUS16  GOPRO_Q_REG_STATUS16    /* 2-byte ids, MISSION 1 */
#define QRY_STATUS_PUSH16 GOPRO_Q_PUSH_STATUS16
#define QRY_REG_SETTING   0x52
#define QRY_SETTING_PUSH  0x92
#define SET_RES           2
#define SET_FPS           3

/* Status IDs we read. */
#define ST_HOT        6
#define ST_BUSY       8
#define ST_ENCODING   10
#define ST_VIDEO_S    13
#define ST_SD_STATUS  33
#define ST_LEFT_S     35
#define ST_BATT_PCT   70
#define ST_PRESET_GRP 96   /* which preset group -- video, photo or timelapse */
#define ST_PRESET_ID  97   /* active preset id; group in the upper 16 bits     */

#define KEEPALIVE_MS      3000
#define RECONNECT_MS      3000
#define PAIR_FAIL_WAIT_MS 10000
#define OPEN_TIMEOUT_MS   15000
#define MSG_MAX           600

/* ---- state -------------------------------------------------------------- */

static SemaphoreHandle_t s_lock;
static gopro_status_t    s_st;
static uint32_t          s_last_status_ms;

/* The connection state -- address, GATTC interface, connection id, service
 * range, the six characteristic handles, the connected / encrypted / ready /
 * opening flags -- belongs to the shared transport, not here. What this file
 * keeps is the one thing the transport cannot know: whether this session still
 * WANTS a camera, which only sleep and wake move. */
static bool          s_want_connect = true;
static uint32_t      s_last_ka_ms;

static bool s_rec_pending;
static bool s_rec_value;
static bool s_sleep_pending;
static bool s_hilight_pending;
static bool s_reg_all_fallback;

/* The status-id family in use. s_wide is the model's suggestion until a
 * registration reply confirms or corrects it; s_reg_other_tried records that
 * the other family has already been offered, so a camera that refuses both
 * is asked for ALL statuses once and not alternately for ever. The keep-alive
 * has its own flag and its own verdict, judged on the settings replies. */
static bool     s_wide;
static bool     s_reg_other_tried;
static bool     s_ka_wide;
static uint8_t  s_ka_flips;
static uint8_t  s_ka_sent;
static uint16_t s_set_replies;
static bool     s_set_refused;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ---- packet reassembly -------------------------------------------------- */

typedef struct {
    uint8_t  buf[MSG_MAX];
    size_t   expect;
    size_t   got;
    bool     active;
} reasm_t;

static reasm_t s_rx[3];   /* indexed by CH_*_RSP - CH_CMD_RSP */

/* Returns true when a whole message is in r->buf (length r->expect). */
static bool reasm_feed(reasm_t *r, const uint8_t *p, size_t n)
{
    if (n == 0) return false;
    uint8_t h = p[0];
    if (h & 0x80) {
        if (!r->active) return false;          /* continuation with no start */
        p++; n--;
    } else {
        size_t len, skip;
        switch ((h >> 5) & 0x03) {
        case 0:  len = h & 0x1F; skip = 1; break;
        case 1:  if (n < 2) return false; len = ((size_t)(h & 0x1F) << 8) | p[1]; skip = 2; break;
        case 2:  if (n < 3) return false; len = ((size_t)p[1] << 8) | p[2]; skip = 3; break;
        default: return false;
        }
        r->active = true; r->expect = len; r->got = 0;
        p += skip; n -= skip;
    }
    size_t room = (r->expect > r->got) ? r->expect - r->got : 0;
    if (n > room) n = room;
    if (r->got + n > sizeof(r->buf)) n = sizeof(r->buf) - r->got;
    memcpy(r->buf + r->got, p, n);
    r->got += n;
    if (r->got >= r->expect) { r->active = false; return true; }
    return false;
}

static void hexdump(const char *what, const uint8_t *p, size_t n)
{
    char hex[3 * 48 + 4];
    size_t w = 0;
    for (size_t i = 0; i < n && i < 48; i++) w += (size_t)snprintf(hex + w, sizeof(hex) - w, "%02X ", p[i]);
    if (n > 48) snprintf(hex + w, sizeof(hex) - w, "..");
    ESP_LOGI(TAG, "%s (%u) [%s]", what, (unsigned)n, hex);
}

/* ---- writes ------------------------------------------------------------- */

/* C3CAMLINK PATCH: was a direct esp_ble_gattc_write_char() against handles this
 * file discovered itself. The transport owns the handles now, so a write names
 * a CHANNEL and the transport resolves it -- which is what lets the same three
 * channels mean different characteristics for a different vendor. It also
 * answers "are we connected?" in one place instead of two. */
static bool write_ch(int ch, const uint8_t *bytes, size_t n)
{
    esp_err_t e = ble_chan_write(ch, bytes, n);
    if (e != ESP_OK) ESP_LOGW(TAG, "write %s failed: %s", s_ch_name[ch], esp_err_to_name(e));
    return e == ESP_OK;
}

static void send_keepalive(void)
{
    /* Judge the form on what the camera said to the last ones. A refusal is
       explicit; two sends with no reply at all is the same verdict said
       quietly. Either way switch -- but at most twice. */
    bool flip = false;
    if (s_set_refused) { flip = true; s_set_refused = false; }
    else if (s_ka_sent >= 2 && s_set_replies == 0) flip = true;
    if (flip && s_ka_flips < 2) {
        s_ka_wide = !s_ka_wide;
        s_ka_flips++;
        s_ka_sent = 0;
        ESP_LOGW(TAG, "keep-alive not acknowledged; switching to the %s form",
                 s_ka_wide ? "2-byte" : "1-byte");
    }
    /* "instead of using the Setting ID as the type byte, a 0xFF marker byte
       is used as the type, followed by [id hi][id lo][len][value]". */
    static const uint8_t ka_narrow[] = { 0x03, SET_KEEP_ALIVE, 0x01, 0x42 };
    static const uint8_t ka_wide[]   = { 0x05, GOPRO_SET_WIDE_MARKER, 0x00, SET_KEEP_ALIVE, 0x01, 0x42 };
    bool ok = s_ka_wide ? write_ch(CH_SET, ka_wide,   sizeof(ka_wide))
                        : write_ch(CH_SET, ka_narrow, sizeof(ka_narrow));
    if (ok) s_ka_sent++;
}

static void send_shutter(bool on)
{
    /* Refuse to START in a preset that photographs. Stopping is always allowed:
     * a stop that does nothing is harmless, and a stop that is refused because
     * we misread the preset would leave the camera running with no way to put
     * it down. See gopro_preset_takes_stills(). */
    if (on) {
        bool blocked = false;
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
            blocked = gopro_preset_takes_stills(s_st.preset_group, s_st.preset_valid);
            xSemaphoreGive(s_lock);
        }
        if (blocked) {
            /* WARN, not INFO: this is the module declining to do the one thing
             * it was asked for, and it must never be invisible. */
            ESP_LOGW(TAG, "NOT starting: the camera is in a photo or timelapse "
                          "preset, so the shutter would take stills, not video");
            return;
        }
    }
    const uint8_t sh[] = { 0x03, CMD_SET_SHUTTER, 0x01, on ? 0x01 : 0x00 };
    ESP_LOGI(TAG, "shutter %s", on ? "ON" : "OFF");
    write_ch(CH_CMD, sh, sizeof(sh));
}

static void send_hw_info(void)
{
    static const uint8_t q[] = { 0x01, CMD_HW_INFO };
    write_ch(CH_CMD, q, sizeof(q));
}

/* 97 alongside 96: the HERO (2024) accepts both individually but only 97
 * tells the truth about a photo preset. 43 (older HEROs' current mode) is NOT
 * here: that camera refuses any batch that contains it, which would drop the
 * whole registration. One list, so the two families cannot drift apart. */
static const uint8_t k_status_ids[] = {
    ST_ENCODING, ST_VIDEO_S, ST_LEFT_S, ST_BATT_PCT,
    ST_HOT, ST_BUSY, ST_SD_STATUS, ST_PRESET_GRP, ST_PRESET_ID,
};

static void send_register_status(bool all, bool wide)
{
    uint8_t q[2 + 2 * sizeof(k_status_ids)];
    size_t o = 1;
    q[o++] = wide ? QRY_REG_STATUS16 : QRY_REG_STATUS;
    if (all) {
        ESP_LOGW(TAG, "registering for ALL status pushes (%s ids)", wide ? "2-byte" : "1-byte");
    } else {
        for (size_t k = 0; k < sizeof(k_status_ids); k++) {
            if (wide) q[o++] = 0x00;            /* every id we use is < 256 */
            q[o++] = k_status_ids[k];
        }
    }
    q[0] = (uint8_t)(o - 1);
    write_ch(CH_QRY, q, o);
}

/* ---- parsing ------------------------------------------------------------ */

static uint32_t be_val(const uint8_t *p, uint8_t len)
{
    uint32_t v = 0;
    for (uint8_t i = 0; i < len && i < 4; i++) v = (v << 8) | p[i];
    return v;
}

static void label_from_model(const char *model, char out[6])
{
    /* "HERO13 Black" -> "H13", "HERO" -> "HERO", "MAX 2" -> "MAX2". Four
     * characters is what the OSD field has; keep the digits, they are the
     * part that tells two GoPros apart. */
    char letters[8] = {0}, digits[8] = {0};
    size_t l = 0, d = 0;
    for (const char *p = model; *p && (l < 7 || d < 7); p++) {
        if (isdigit((unsigned char)*p)) { if (d < 7) digits[d++] = *p; }
        else if (isalpha((unsigned char)*p) && l < 7 && d == 0) letters[l++] = (char)toupper((unsigned char)*p);
        else if (*p == ' ' && d) break;
    }
    if (d == 0) { strlcpy(out, letters[0] ? letters : "GPRO", 5); return; }
    size_t keep = 4 - (d > 3 ? 3 : d);
    if (keep > l) keep = l;
    memcpy(out, letters, keep);
    memcpy(out + keep, digits, d > 3 ? 3 : d);
    out[keep + (d > 3 ? 3 : d)] = '\0';
}

/* Enum -> label for the video resolution and framerate settings. These enum
 * numbers are the long-standing Open GoPro values, stable across HERO5 and up
 * for the common modes; an unrecognised value is shown as its raw number rather
 * than guessed at, since the far ends of the table are model-specific. */
static const char *res_label(uint32_t v)
{
    switch (v) {
    case 1: case 2: case 18: case 28: case 103: case 108: return "4K";
    case 4: case 5: case 6: case 111:                     return "2.7K";
    case 7:                                               return "1440";
    case 8: case 9:                                       return "1080";
    case 10:                                              return "960";
    case 11: case 12:                                     return "720";
    case 13:                                              return "480";
    case 24: case 25:                                     return "5K";
    case 26: case 27: case 100: case 102: case 107:       return "5.3K";
    default:                                              return NULL;
    }
}
static const char *fps_label(uint32_t v)
{
    switch (v) {
    case 0:  return "240"; case 1: return "120"; case 2: return "100";
    case 3:  return "90";  case 4: return "80";  case 5: return "60";
    case 6:  return "50";  case 7: return "48";  case 8: return "30";
    case 9:  return "25";  case 10: return "24"; case 13: return "200";
    default: return NULL;
    }
}

static void send_register_settings(void)
{
    /* Register for ALL settings (empty id list), not just resolution and
     * framerate. Simpler cameras -- the HERO (2024) among them -- do not answer
     * a request for specific setting ids, but do answer the register-all form;
     * its response carries every current value and later pushes only the ones
     * that change. We keep only resolution (2) and framerate (3). */
    static const uint8_t q[] = { 0x01, QRY_REG_SETTING };
    write_ch(CH_QRY, q, sizeof(q));
}

static void on_setting_values(const uint8_t *p, size_t n)
{
    size_t i = 0;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) != pdTRUE) return;
    while (i + 2 <= n) {
        uint8_t id = p[i], len = p[i + 1];
        if (i + 2 + len > n) break;
        uint32_t val = be_val(p + i + 2, len);
#if CONFIG_CAMLINK_GPSETTINGS_DUMP
        ESP_LOGW(TAG, "SETTING %u len %u = %u", id, len, (unsigned)val);
#endif
        if (id == SET_RES) {
            const char *l = res_label(val);
            if (l) strlcpy(s_st.res, l, sizeof(s_st.res));
            else   snprintf(s_st.res, sizeof(s_st.res), "%u", (unsigned)val);
        } else if (id == SET_FPS) {
            const char *l = fps_label(val);
            if (l) strlcpy(s_st.fps, l, sizeof(s_st.fps));
            else   snprintf(s_st.fps, sizeof(s_st.fps), "%u", (unsigned)val);
        }
        i += 2 + len;
    }
    xSemaphoreGive(s_lock);
}

static void on_status_values(const uint8_t *p, size_t n, bool push, bool wide)
{
    /* [id][len][value]... or, in the wide family, [id hi][id lo][len][value] */
    size_t i = 0;
    uint16_t id; uint8_t len; const uint8_t *v;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) != pdTRUE) return;
    while (gopro_status_next(p, n, wide, &i, &id, &len, &v)) {
        uint32_t val = be_val(v, len);
        switch (id) {
        case ST_ENCODING:  s_st.encoding = val != 0; break;
        case ST_VIDEO_S:   s_st.clip_s = val; break;
        case ST_LEFT_S:    s_st.left_s = val; break;
        case ST_BATT_PCT:  s_st.battery_pct = (uint8_t)(val > 100 ? 100 : val); s_st.battery_valid = true; break;
        case ST_HOT:       s_st.hot = (uint8_t)val; break;
        case ST_BUSY:      s_st.busy = val != 0; break;
        case ST_SD_STATUS: s_st.sd_ok = (val == 0); break;
        case ST_PRESET_ID:
            s_st.preset_id       = val;
            s_st.preset_id_valid = true;
            s_st.preset_group    = gopro_preset_group_from_id(val);
            s_st.preset_valid    = true;
            s_st.can_record      = !gopro_preset_takes_stills(s_st.preset_group, true);
            break;
        case ST_PRESET_GRP:
            /* Only until 97 has spoken: on the HERO (2024) this one says
             * VIDEO on a photo preset. It is kept for cameras that report
             * 96 but not 97. */
            if (!s_st.preset_id_valid) {
                s_st.preset_group = val;
                s_st.preset_valid = true;
                s_st.can_record   = !gopro_preset_takes_stills(val, true);
            }
            break;
        default:
            ESP_LOGD(TAG, "status %u len %u = %u", id, len, (unsigned)val);
            break;
        }
        if (push && id != ST_VIDEO_S) {
            ESP_LOGI(TAG, "status %u = %u", id, (unsigned)val);
        }
    }
    s_last_status_ms = now_ms();
    s_st.status_valid = true;
    xSemaphoreGive(s_lock);
}

static void on_message(int rsp_ch, const uint8_t *m, size_t n)
{
    if (n < 2) { hexdump("short message", m, n); return; }
    uint8_t id = m[0], status = m[1];

    if (rsp_ch == CH_CMD_RSP) {
        if (id == CMD_HW_INFO && status == 0) {
            /* [len][model number][len][model name][len][board type][len][fw]... */
            size_t i = 2;
            if (i < n) { uint8_t l = m[i]; i += 1 + l; }          /* model number */
            if (i < n) {
                uint8_t l = m[i++];
                if (i + l <= n) {
                    size_t c = l < sizeof(s_st.model) - 1 ? l : sizeof(s_st.model) - 1;
                    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
                        memcpy(s_st.model, m + i, c); s_st.model[c] = '\0';
                        label_from_model(s_st.model, s_st.label);
                        xSemaphoreGive(s_lock);
                    }
                    ESP_LOGI(TAG, "camera model \"%s\" -> label %s", s_st.model, s_st.label);
                }
                i += l;
            }
            /* Firmware version is worth one log line for a camera nobody has
             * seen this firmware talk to before. */
            if (i < n) { uint8_t l = m[i]; i += 1 + l; }          /* board type */
            if (i < n) {
                uint8_t l = m[i++];
                if (i + l <= n) ESP_LOGI(TAG, "camera firmware %.*s", (int)l, (const char *)m + i);
            }
            return;
        }
        ESP_LOGI(TAG, "command 0x%02X -> status %u", id, status);
        if (status != 0) hexdump("command response", m, n);
        return;
    }
    if (rsp_ch == CH_SET_RSP) {
        /* A settings reply is the only thing that says whether the keep-alive
         * form was right. Counted as well as judged: a camera that never
         * answers at all is also a camera in the wrong form. */
        uint16_t sid = 0; uint8_t sst = 0;
        if (gopro_setting_reply(m, n, &sid, &sst)) {
            s_set_replies++;
            if (sst != 0) s_set_refused = true;
            if (sid == SET_KEEP_ALIVE && sst == 0) return;   /* every 3 s; silence */
            ESP_LOGI(TAG, "setting 0x%02X -> status %u", sid, sst);
        }
        return;
    }
    /* query response */
    if (id == QRY_REG_STATUS || id == QRY_REG_STATUS16) {
        bool wide = (id == QRY_REG_STATUS16);
        if (status != 0) {
            ESP_LOGW(TAG, "status registration (%s ids) refused (status %u)",
                     wide ? "2-byte" : "1-byte", status);
            /* The ladder: our list in the model's family, then our list in
             * the other family, then ALL statuses in the model's family. A
             * refusal is the camera's verdict on the FAMILY first -- a
             * MISSION 1 says no to 1-byte ids however good the list is. */
            if (!s_reg_other_tried) {
                s_reg_other_tried = true;
                ESP_LOGW(TAG, "trying the other status-id family");
                send_register_status(false, !wide);
            } else if (!s_reg_all_fallback) {
                s_reg_all_fallback = true;
                send_register_status(true, s_wide);
            }
            return;
        }
        if (wide != s_wide) {
            ESP_LOGW(TAG, "camera answers in the %s family, not the one its model suggested",
                     wide ? "2-byte" : "1-byte");
        }
        s_wide = wide;
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) { s_st.wide_ids = wide; xSemaphoreGive(s_lock); }
        ESP_LOGI(TAG, "status registration accepted (%s ids), %u bytes of current values",
                 wide ? "2-byte" : "1-byte", (unsigned)(n - 2));
        on_status_values(m + 2, n - 2, false, wide);
        return;
    }
    if (id == QRY_STATUS_PUSH)   { on_status_values(m + 2, n - 2, true, false); return; }
    if (id == QRY_STATUS_PUSH16) { on_status_values(m + 2, n - 2, true, true);  return; }
    if (id == QRY_REG_SETTING || id == QRY_SETTING_PUSH) { on_setting_values(m + 2, n - 2); return; }
    ESP_LOGI(TAG, "query 0x%02X -> status %u", id, status);
    hexdump("query response", m, n);
}

/* ---- session over the shared transport ----------------------------------
 *
 * The BLE controller is NOT owned here. Bluedroid bring-up, the GAP and GATTC
 * callbacks, the scan, the connect, the bonding and the reconnect loop all
 * live once in components/dji_link/ble/ and are shared with the DJI session.
 *
 * That is what lets one module hold either kind of camera without rebooting:
 * two stacks could not both exist, so a per-vendor transport would make
 * switching between saved cameras a restart. What is left in this file is the
 * part that is actually about GoPro -- the GATT layout, the framing, and the
 * protocol.
 * -------------------------------------------------------------------------- */

/* The GATT layout, declared once so the transport can find the
 * characteristics the instant the scan picks a GoPro.
 *
 * THE SERVICE UUID IS NOT IN GOPRO'S BASE. It is a SIG-assigned 16-bit value,
 * so its 128-bit form expands with the standard Bluetooth base and not with
 * b5f9. GoPro's own documentation lists it in the same table as the GP-XXXX
 * shorthand, which invites exactly the wrong expansion. Declared here as the
 * 16-bit value it actually is, which sidesteps the question. */
#define GP_UUID128(hi, lo) { \
    0x1b, 0xc5, 0xd5, 0xa5, 0x02, 0x00, 0x46, 0x90, \
    0xe3, 0x11, 0x8d, 0xaa, (lo), (hi), 0xf9, 0xb5 }

static const ble_gatt_profile_t s_gopro_profile = {
    .name    = "GoPro",
    .service = { .len = ESP_UUID_LEN_16, .uuid.uuid16 = GP_SVC_UUID16 },
    .chan = {
        [CH_CMD] = { .write  = { .len = ESP_UUID_LEN_128, .uuid.uuid128 = GP_UUID128(0x00, 0x72) },
                     .notify = { .len = ESP_UUID_LEN_128, .uuid.uuid128 = GP_UUID128(0x00, 0x73) } },
        [CH_SET] = { .write  = { .len = ESP_UUID_LEN_128, .uuid.uuid128 = GP_UUID128(0x00, 0x74) },
                     .notify = { .len = ESP_UUID_LEN_128, .uuid.uuid128 = GP_UUID128(0x00, 0x75) } },
        [CH_QRY] = { .write  = { .len = ESP_UUID_LEN_128, .uuid.uuid128 = GP_UUID128(0x00, 0x76) },
                     .notify = { .len = ESP_UUID_LEN_128, .uuid.uuid128 = GP_UUID128(0x00, 0x77) } },
    },
    .n_chan = 3,
};

void gopro_cam_select(void)
{
    ble_register_vendor_profile(CAM_VENDOR_GOPRO, &s_gopro_profile);
}

/* Every notification the transport receives while a GoPro is the connected
 * camera. Runs on the Bluedroid callback task. */
void gopro_cam_on_notify(uint16_t handle, const uint8_t *data, size_t len)
{
    int chan = ble_chan_of_notify_handle(handle);
    if (chan < 0 || chan > CH_QRY) {
        /* Not one of ours. Ignored rather than fed to a reassembler, which
         * would then be out of step for every message after it. */
        hexdump("notify from unknown handle", data, len);
        return;
    }

    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG);

    /* The response channels keep their own reassemblers: the three streams are
     * independent and their packets interleave, so a status push can land
     * between the two halves of a command reply. One reassembler would splice
     * them into a message that never existed. */
    reasm_t *r = &s_rx[chan];
    if (reasm_feed(r, data, len)) on_message(CH_CMD_RSP + chan, r->buf, r->expect);
}

/* ---- session ------------------------------------------------------------ */

/* Bring the protocol up on a link the transport has already made, bonded and
 * discovered. Returns false if the camera is not answering, which the caller
 * turns into a dropped link and a retry.
 *
 * `model` is the advertised model id, or 0 when it could not be read. It is
 * only used to choose which status-id family to try FIRST; the camera's own
 * answer decides, so an unknown model costs a round trip and nothing else.
 * That matters here: this firmware binds cameras GoPro does not list, and some
 * of them report no model at all. */
bool gopro_cam_session_start(uint8_t model)
{
    if (s_lock == NULL) s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        /* Every status read takes this lock and returns nothing without it, so
         * the camera would connect and report absolutely nothing, for ever,
         * with no explanation. Refuse the session instead. */
        ESP_LOGE(TAG, "could not create the session lock");
        return false;
    }

    memset(s_rx, 0, sizeof(s_rx));
    s_reg_all_fallback = false;
    s_reg_other_tried  = false;
    s_wide     = cam_gopro_wide_ids(model);
    s_ka_wide  = s_wide;
    s_ka_flips = 0; s_ka_sent = 0; s_set_replies = 0; s_set_refused = false;
    if (s_wide) ESP_LOGI(TAG, "model %u: trying the 2-byte status-id family first", model);
    s_rec_pending = s_sleep_pending = s_hilight_pending = false;
    s_last_ka_ms = 0;

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        memset(&s_st, 0, sizeof(s_st));
        cam_gopro_label(model, s_st.label);
        s_st.link_up = true;
        /* Until the camera says otherwise, assume the shutter films. The memset
         * above would otherwise leave this false, which would silently refuse
         * to record on every camera that does not report status 96 at all. */
        s_st.can_record = true;
        xSemaphoreGive(s_lock);
    }

    /* Wait for all three response characteristics to be subscribed.
     *
     * This is what the old s_ready flag guaranteed, and it is not optional: a
     * command sent before its reply channel is listening gets an answer that
     * lands nowhere, and the session then waits for ever on a camera that
     * already answered. BLE_CONNECTED only promises the link and the handles;
     * the CCCD writes are still in flight behind it. */
    bool ready = false;
    for (int i = 0; i < 60; i++) {          /* 3 s, well past a normal link-up */
        if (ble_channels_ready()) { ready = true; break; }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!ready) {
        ESP_LOGE(TAG, "not all channels subscribed -- dropping the link");
        return false;
    }

    ESP_LOGI(TAG, "session up (%s)", cam_gopro_model_name(model));

    /* Order matters and is the order the old link-up path used: keep-alive
     * first so the camera does not time us out mid-handshake, then identify
     * it, then subscribe to what it reports. */
    send_keepalive();
    send_hw_info();
    send_register_status(false, s_wide);
    send_register_settings();
    return true;
}

/* One pass of the session, called by the camera task while the link is up. */
void gopro_cam_tick(void)
{
    uint32_t t = now_ms();

    if ((t - s_last_ka_ms) >= KEEPALIVE_MS) { s_last_ka_ms = t; send_keepalive(); }

    if (s_rec_pending) { s_rec_pending = false; send_shutter(s_rec_value); }

    if (s_hilight_pending) {
        s_hilight_pending = false;
        static const uint8_t hl[] = { 0x01, 0x18 };   /* Tag HiLight */
        ESP_LOGI(TAG, "HiLight tag");
        write_ch(CH_CMD, hl, sizeof(hl));
    }

    if (s_sleep_pending) {
        s_sleep_pending = false;
        static const uint8_t z[] = { 0x01, CMD_SLEEP };
        ESP_LOGW(TAG, "sending sleep");
        write_ch(CH_CMD, z, sizeof(z));
        /* The transport reconnects on its own, and reconnecting to a GoPro is
         * exactly what wakes it -- so a sleep that did not also stop us
         * reaching for the camera would put it to sleep and immediately wake
         * it again, for ever. The camera task reads this and drops the link;
         * gopro_cam_wake() lifts it. */
        s_want_connect = false;
    }
}

/* The link has gone, or is about to. Forget everything the camera told us.
 *
 * The whole reading goes, not just the link flag: s_st outlives the session,
 * and a leftover overheat or full card would follow the module into its next
 * connection -- possibly to a different camera that never reports either and
 * so could never clear it. */
void gopro_cam_session_end(void)
{
    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        memset(&s_st, 0, sizeof(s_st));
        xSemaphoreGive(s_lock);
    }
    memset(s_rx, 0, sizeof(s_rx));
    s_last_status_ms = 0;
    s_reg_all_fallback = false;
}

/* ---- API ---------------------------------------------------------------- */

void gopro_cam_get(gopro_status_t *out)
{
    if (out == NULL) return;
    if (s_lock == NULL || xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) != pdTRUE) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = s_st;
    out->status_age_ms = s_last_status_ms ? (now_ms() - s_last_status_ms) : UINT32_MAX;
    xSemaphoreGive(s_lock);
}

void gopro_cam_request_record(bool on) { s_rec_value = on; s_rec_pending = true; }
void gopro_cam_sleep(void)             { s_sleep_pending = true; }
void gopro_cam_hilight(void)           { s_hilight_pending = true; }

bool gopro_cam_wants_camera(void) { return s_want_connect; }

/* Waking means "stop refusing a camera", not "start connecting": the transport
 * does the reaching, so all this has to do is flip the want back on and the
 * camera task picks the link up on its next pass.
 *
 * Deliberately NOT ble_set_reconnecting(): that selects how the scan MATCHES
 * -- previous address versus the bound list -- and is not a switch for whether
 * to connect at all. Driving it from here would quietly change how the DJI
 * path finds its camera. */
void gopro_cam_wake(void)
{
    s_want_connect = true;
}

/* Drop the link politely and stop reconnecting.
 *
 * Still worth doing before a reboot, and for the same reason as before: a link
 * that just vanishes leaves the camera waiting out its supervision timeout,
 * during which it neither advertises nor accepts a connection -- which is
 * exactly the window the next boot wants to find it in. */
void gopro_cam_disconnect(void)
{
    s_want_connect = false;
    ble_disconnect();
}

bool gopro_cam_write(gopro_ch_t ch, const uint8_t *bytes, size_t n)
{
    int c = ch == GOPRO_CH_CMD ? CH_CMD : ch == GOPRO_CH_SET ? CH_SET : CH_QRY;
    return write_ch(c, bytes, n);
}
