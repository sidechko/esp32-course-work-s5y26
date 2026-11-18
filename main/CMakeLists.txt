idf_component_register(SRCS "blink_example_main.c" "display_content.h"
        PRIV_REQUIRES esp_driver_pcnt esp_driver_gpio esp_adc esp_timer
        INCLUDE_DIRS ".")
