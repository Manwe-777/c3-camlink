idf_component_register(SRCS "camlink.c" "osd_format.c" "camlink_cfg.c"
                       INCLUDE_DIRS "include"
                       # dji_link is for camlink_ble_apply_tx_level() and board for the rung it
                       # takes, which is how the radio is turned down while armed. Both resolved
                       # transitively through duml before being declared, which works right up
                       # until duml stops needing them.
                       REQUIRES msp duml dji_link board esp_timer nvs_flash)
