//
// Created by s1pepega on 21.11.2026.
//

#ifndef CWS5Y26_ENCODER_H
#define CWS5Y26_ENCODER_H

#endif //CWS5Y26_ENCODER_H
#include "driver/pulse_cnt.h"
#include "esp_rom_gpio.h"
#include "driver/gpio.h"
#include "../state_info.h"

void init_encoder(pcnt_unit_handle_t * pcnt_unit, int min, int max, uint32_t glitch, int clk_gpio, int dt_gpio);
void encoder_read(pcnt_unit_handle_t * pcnt_unit, int * pulse_count, int * prev_pulse_count, state_t * state, int * selected_menu, int * selected_num);
void init_encoder_button(int sw_gpio, gpio_isr_t gpio_isr_handler);