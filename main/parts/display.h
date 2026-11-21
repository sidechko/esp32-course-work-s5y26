//
// Created by s1pepega on 21.11.2026.
//

#ifndef CWS5Y26_DISPLAY_H
#define CWS5Y26_DISPLAY_H
#include "ssd1306.h"
#include "../state_info.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"

#endif //CWS5Y26_DISPLAY_H

void init_i2c_display(i2c_master_bus_handle_t bus_handle, i2c_device_config_t * config, int address, SSD1306_t* dev);
void display_static_content(SSD1306_t* dev);
void display_dynamic_content(SSD1306_t* dev, state_t * state, int selected_num, int selected_menu, int acb_seg_count);
void display_selected_number(SSD1306_t* dev, int selected_num);
void display_selected_menu(SSD1306_t* dev, int selected_menu);
void display_acb(SSD1306_t* dev, int acb_seg_count);