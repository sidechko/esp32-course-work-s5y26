idf_component_register(
        SRCS
        "cws5y26.c" "core.h"
        "parts/cw_pn532.h" "parts/cw_pn532.c"
        "parts/display.h" "parts/display.c"
        "parts/encoder.h" "parts/encoder.c"
        "parts/battery.h" "parts/battery.c"
        "storage.h" "storage.c"
        "card_detect.h" "card_detect.c"
        INCLUDE_DIRS "."
        PRIV_REQUIRES esp_driver_pcnt esp_driver_gpio esp_adc esp_timer nvs_flash
        INCLUDE_DIRS ".")
