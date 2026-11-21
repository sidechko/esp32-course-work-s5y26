//
// Created by s1pepega on 21.11.2026.
//
#include "display.h"
#include "display_content.h"

void init_i2c_display(i2c_master_bus_handle_t bus_handle, i2c_device_config_t * config, int address, SSD1306_t* dev)
{
    i2c_master_dev_handle_t dev_handle;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, config, &dev_handle));
    dev->_address = address;
    dev->_flip = true;
    dev->_i2c_num = I2C_NUM_0;
    dev->_i2c_bus_handle = bus_handle;
    dev->_i2c_dev_handle = dev_handle;

    ssd1306_init(dev, 128, 64);
    ssd1306_clear_screen(dev, false);
    ssd1306_contrast(dev, 0xff);
}
void display_acb(SSD1306_t* dev, int acb_seg_count)
{
    for (int y = 1; y < 6; y++)
    {
        hud_acb[y*4] &= 0x80;
        for (int x = 1; x < 3; x++)
            hud_acb[y * 4 + x] = 0x00;
        hud_acb[y*4+3] &= 0x03;
    }
    uint8_t map[] = {0x80, 0x00, 0x00, 0x02};
    int c = acb_seg_count + 1;
    int mv = 0;
    for (int _m = 0; _m < 4; _m++)
    {
        while (mv < 8)
        {
            map[_m] |=  1 << (7 - mv);
            mv++;
            if (--c == 0) break;
        }
        mv = 0;
        if (c == 0) break;
    }
    for (int _m = 0; _m < 4; _m++)
    {
        hud_acb[4+_m] |= map[_m];
        hud_acb[8+_m] |= map[_m];
        hud_acb[12+_m] |= map[_m];
        hud_acb[16+_m] |= map[_m];
        hud_acb[20+_m] |= map[_m];
    }
    ssd1306_bitmaps(dev, 0, 0, hud_acb, 32, 7, false);
}

void display_static_content(SSD1306_t* dev)
{
    ssd1306_bitmaps(dev, 76, 15, hud_slash, 16, 34, false);
    ssd1306_bitmaps(dev, 92, 15, seg2, 16, 34, false);
    ssd1306_bitmaps(dev, 108, 15, seg0, 16, 34, false);
}
void display_dynamic_content(SSD1306_t* dev, state_t * state, int selected_num, int selected_menu, int acb_seg_count)
{
    if (*state & ACB_UPDATE_BIT){
        display_acb(dev, acb_seg_count);
    }
    if (*state & NUM_CHANGE_BIT){
        display_selected_number(dev, selected_num);
    }
    if (*state & MENU_CHANGE_BIT){
        display_selected_menu(dev, selected_menu);
    }
}


void display_selected_number(SSD1306_t* dev, int selected_num)
{
    if (selected_num > 9)
    {
        if (selected_num > 19)
            ssd1306_bitmaps(dev, 45, 15, seg2, 16, 34, false);
        else
            ssd1306_bitmaps(dev, 45, 15, seg1, 16, 34, false);
    }
    else
    {
        ssd1306_bitmaps(dev, 45, 15, seg0, 16, 34, false);
    }
    switch (selected_num % 10)
    {
    case 1:
        ssd1306_bitmaps(dev, 61, 15, seg1, 16, 34, false);
        break;
    case 2:
        ssd1306_bitmaps(dev, 61, 15, seg2, 16, 34, false);
        break;
    case 3:
        ssd1306_bitmaps(dev, 61, 15, seg3, 16, 34, false);
        break;
    case 4:
        ssd1306_bitmaps(dev, 61, 15, seg4, 16, 34, false);
        break;
    case 5:
        ssd1306_bitmaps(dev, 61, 15, seg5, 16, 34, false);
        break;
    case 6:
        ssd1306_bitmaps(dev, 61, 15, seg6, 16, 34, false);
        break;
    case 7:
        ssd1306_bitmaps(dev, 61, 15, seg7, 16, 34, false);
        break;
    case 8:
        ssd1306_bitmaps(dev, 61, 15, seg8, 16, 34, false);
        break;
    case 9:
        ssd1306_bitmaps(dev, 61, 15, seg9, 16, 34, false);
        break;
    default:
        ssd1306_bitmaps(dev, 61, 15, seg0, 16, 34, false);
    }
}

void display_selected_menu(SSD1306_t* dev, int selected_menu)
{
    ssd1306_bitmaps(dev, 0, 14, hud_button_read, 40, 9, selected_menu == 0);
    ssd1306_bitmaps(dev, 0, 24, hud_button_write, 40, 9, selected_menu == 1);
    ssd1306_bitmaps(dev, 0, 34, hud_button_use, 40, 9, selected_menu == 2);
    ssd1306_bitmaps(dev, 0, 44, hud_button_exit, 40, 9, selected_menu == 3);
}