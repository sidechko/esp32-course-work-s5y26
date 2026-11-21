idf_component_register(
        SRCS "cws5y26.c" "display_content.h"
        PRIV_REQUIRES esp_driver_pcnt esp_driver_gpio esp_adc esp_timer
        INCLUDE_DIRS ".")
