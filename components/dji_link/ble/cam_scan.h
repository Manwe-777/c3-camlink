/* SPDX-License-Identifier: MIT
 *
 * Config-mode camera scanner: discovery only, never connects. See cam_scan.c.
 */
#ifndef CAMLINK_CAM_SCAN_H
#define CAMLINK_CAM_SCAN_H

#include <stdint.h>
#include "esp_err.h"
/* C3CAMLINK ADDITION: cam_vendor_t and the GoPro advert parsers live in the
 * camvendor leaf component, so the GoPro session can share them without a
 * build cycle. Note that CAM_VENDOR_NONE is 0 there, so a zeroed record is not
 * a camera. That is only safe because the vendor byte is never written to NVS;
 * see the note on the enum itself. */
#include "camvendor.h"

#define CAM_SCAN_MAX       12
#define CAM_SCAN_NAME_LEN  24

typedef struct {
    uint8_t  bda[6];
    char     name[CAM_SCAN_NAME_LEN];
    int8_t   rssi;
    uint8_t  model;      /* Vendor's own model code, 0 = unidentified. Which
                          * namespace it is in is decided by `vendor`: a DJI
                          * manufacturer code, or a GoPro model id read from
                          * the advert. Never compare one without the other. */
    uint8_t  vendor;     /* cam_vendor_t                                   */
    uint8_t  addr_type;  /* esp_ble_addr_type_t as advertised. GoPros use a
                          * static random address, and a connection request
                          * with the wrong type is never answered.        */
    uint32_t last_ms;
} cam_scan_entry_t;

/* Bring up BLE and start scanning. Call instead of ble_init(): this path never
 * connects, and the two must not both own the controller. */
esp_err_t cam_scan_start(void);

/* Stop scanning and power the Bluetooth controller down, freeing the radio for
 * Wi-Fi. The captured list survives for the picker. See cam_scan.c. */
void cam_scan_stop(void);

/* Snapshot, strongest signal first, stale entries already dropped.
 * Returns how many were written. */
int cam_scan_get(cam_scan_entry_t *out, int max);

/* "Osmo Nano", "Osmo Action 5 Pro", or "" when the model code is not one we
 * have actually seen on hardware. */
const char *cam_scan_model_name(uint8_t model);

/* What the picker shows under the name: the DJI model name, or the GoPro model
 * name when the advert gave one ("GoPro HERO (2024)"), or plain "GoPro". */
const char *cam_scan_entry_model(const cam_scan_entry_t *e);

/* C3CAMLINK ADDITION: what to warn the user about this camera before they bind
 * it, or NULL when there is nothing to say. Never a refusal -- see
 * cam_gopro_model_note(). */
const char *cam_scan_entry_note(const cam_scan_entry_t *e);

#endif
