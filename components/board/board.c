/*
 * Transmit-power ladders, one per supported part. See board.h for why this is
 * a rung rather than a level.
 */
#include "board.h"
#include "esp_bt.h"
#include "sdkconfig.h"

/* The ladders are written out per family rather than computed, because the
 * enum values are not comparable across parts and a clever expression here
 * would be a clever expression nobody could check against a datasheet. */
typedef struct { int level; const char *name; } tx_rung_t;

#if CONFIG_IDF_TARGET_ESP32
/* Floor is -12 dBm. ESP_PWR_LVL_N14 exists but is an ALIAS for N12 -- listing
 * both would be two rungs that do the same thing, and a lookup by level could
 * not tell them apart. Three rungs is the honest count. */
static const tx_rung_t k_tx[] = {
    { ESP_PWR_LVL_N12, "-12 dBm" },
    { ESP_PWR_LVL_N0,  "0 dBm"   },
    { ESP_PWR_LVL_P3,  "+3 dBm"  },
};

#elif CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32C61
/* ESP_PWR_LVL_N24 does not exist on this family; the radio stops at -15 dBm.
 * Rungs 0 and 1 are only 3 dB apart as a result, which is closer than the
 * ladder is elsewhere -- kept anyway so the rung count and the settings page
 * stay the same shape on every part. */
static const tx_rung_t k_tx[] = {
    { ESP_PWR_LVL_N15, "-15 dBm" },
    { ESP_PWR_LVL_N12, "-12 dBm" },
    { ESP_PWR_LVL_N0,  "0 dBm"   },
    { ESP_PWR_LVL_P3,  "+3 dBm"  },
};

#else
/* C3, and the S3 which shares the C3's controller outright. The reference
 * ladder, and byte-for-byte what ble.c had hard-coded before this component
 * existed -- which is why a C3 build behaves identically across this change. */
static const tx_rung_t k_tx[] = {
    { ESP_PWR_LVL_N24, "-24 dBm" },
    { ESP_PWR_LVL_N12, "-12 dBm" },
    { ESP_PWR_LVL_N0,  "0 dBm"   },
    { ESP_PWR_LVL_P3,  "+3 dBm"  },
};
#endif

#define TX_COUNT ((int)(sizeof(k_tx) / sizeof(k_tx[0])))

int board_tx_count(void) { return TX_COUNT; }

static const tx_rung_t *rung_at(uint8_t rung)
{
    /* Clamp up, not down. A stored rung past the end of a shorter ladder came
     * from a user asking for MORE power, and answering that with the quietest
     * level would be the opposite of what they chose. Nothing here can select
     * a level the part does not have. */
    if ((int)rung >= TX_COUNT) return &k_tx[TX_COUNT - 1];
    return &k_tx[rung];
}

int         board_tx_level(uint8_t rung) { return rung_at(rung)->level; }
const char *board_tx_name (uint8_t rung) { return rung_at(rung)->name;  }

const char *board_tx_level_name(int level)
{
    for (int i = 0; i < TX_COUNT; i++) {
        if (k_tx[i].level == level) return k_tx[i].name;
    }
    return "(other)";
}
