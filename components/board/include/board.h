/*
 * Per-part facts. Right now that means the radio's transmit ladder and nothing
 * else.
 *
 * WHY ONLY THE RADIO. The other per-part facts -- which pin the LED is on,
 * which the button -- are what a second chip target would also need, and they
 * touch ui.c and Kconfig. They are a separate job. Keeping this module to the
 * radio alone is what keeps it dependency-free.
 *
 * This module depends on NOTHING else in this tree, so anything may depend on
 * it. Keep it that way -- it is what stops the transport and the session
 * components forming a cycle through it.
 *
 * WHAT IT BUYS on a C3-only build: the power levels are not named in ble.c.
 * A bare ESP_PWR_LVL_N24 in that file would quietly make it a C3 file -- the
 * name does not exist on an original ESP32, and on a C6 the radio stops at
 * -15 dBm, so the same name means different things or nothing at all depending
 * on the part. Naming rungs instead of levels keeps the transport portable.
 */
#ifndef CAMLINK_BOARD_H
#define CAMLINK_BOARD_H

#include <stdint.h>

/* --------------------------------------------------------------------------
 * Transmit power
 *
 * A RUNG, not a level. The point of the indirection is that "rung 0" means
 * "as quiet as this chip goes", which is the property that matters when the
 * module sits next to an ELRS receiver -- and what that is worth in dBm is not
 * the same everywhere:
 *
 *     C3 / S3 / C5      -24 dBm     the reference, and what this flies on
 *     C6 / C61          -15 dBm     9 dB louder; its radio goes no quieter
 *     ESP32             -12 dBm    12 dB louder, and only three rungs
 *
 * So a port to a C6 or an original ESP32 is louder next to the receiver than
 * the board this flies on today, and that has to be re-checked with the
 * receiver in the frame rather than assumed to carry over.
 * -------------------------------------------------------------------------- */

/* How many rungs this part actually has. */
int board_tx_count(void);

/* Rung -> esp_power_level_t, as an int so this header stays free of esp_bt.h.
 * A rung at or past board_tx_count() is clamped to the loudest one available,
 * so a stored value from a build with more rungs cannot select a level that
 * does not exist. */
int board_tx_level(uint8_t rung);

/* "-24 dBm". Never NULL: an out-of-range rung gives the clamped one's name. */
const char *board_tx_name(uint8_t rung);

/* The name of a level actually in force, for logging what the radio is doing
 * rather than what was asked for. "(other)" when it is not one of ours. */
const char *board_tx_level_name(int level);

#endif /* CAMLINK_BOARD_H */
