idf_component_register(
        SRCS "cws5y26.c" "state_info.h" "display_content.h" "parts/" "cw_pn532.c" "cw_pn532.h"
        PRIV_REQUIRES esp_driver_pcnt esp_driver_gpio esp_adc esp_timer
        INCLUDE_DIRS ".")
