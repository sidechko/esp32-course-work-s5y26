/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"

#include "ssd1306.h"
#include "display_content.h"

static const char* TAG = "CW";

//APP CONST
#define ENCODER_CLK_GPIO 19
#define ENCODER_DT_GPIO 18
#define ENCODER_SW_GPIO 5
#define ENCODER_MIN_IDX (-40)
#define ENCODER_MAX_IDX 40
#define ENCODER_GLITCH_FILTER_NS 1000

#define I2C_SCL_GPIO 22
#define I2C_SDA_GPIO 21
#define I2C_MASTER_FREQ_HZ 100000
#define DISPLAY_ADDRESS 0x3c
#define PN532_ADDRESS 0x24

#define BATTERY_GPIO_33 32
#define BATTERY_ADC ADC_CHANNEL_4

#define I2C_TICKS_TO_WAIT 500

//DEVICES
SSD1306_t dev;
pcnt_unit_handle_t pcnt_unit;
adc_oneshot_unit_handle_t adc1_handle;

i2c_master_bus_handle_t bus_handle;
i2c_master_dev_handle_t pn532_handle;


// APP STATE
int prev_pulse_count = 0;
int prev_adc_raw = 0;
int pulse_count = 0;
int adc_raw = 0;

int selected_num = 0;
int selected_menu = 4;
bool menu = false;
uint8_t state_changed = UINT8_MAX;

//function
void display_static_content();
void display_selected_number();
void display_selected_menu();
void display_acb();

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    if (menu)
    {
        switch (selected_menu)
        {
        case 0:
            //TODO READ
            break;
        case 1:
            //TODO WRITE
            break;
        case 2:
            //TODO USE
            break;
        default:
            state_changed |= 0b00000100;
            selected_menu = 4;
            break;
        }
    }
    else
    {
        state_changed |= 0b00000100;
        selected_menu = 3;
    }
    menu = !menu;
}

void init()
{
    //I2C master
    i2c_master_bus_config_t i2c_mst_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL_GPIO,
        .sda_io_num = I2C_SDA_GPIO,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &bus_handle));

    //I2C slave display
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DISPLAY_ADDRESS,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    i2c_master_dev_handle_t dev_handle;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));
    if (CONFIG_RESET_GPIO >= 0)
    {
        gpio_reset_pin(CONFIG_RESET_GPIO);
        gpio_set_direction(CONFIG_RESET_GPIO, GPIO_MODE_OUTPUT);
        gpio_set_level(CONFIG_RESET_GPIO, 0);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        gpio_set_level(CONFIG_RESET_GPIO, 1);
    }
    dev._address = DISPLAY_ADDRESS;
    dev._flip = true;
    dev._i2c_num = I2C_NUM_0;
    dev._i2c_bus_handle = bus_handle;
    dev._i2c_dev_handle = dev_handle;

    ssd1306_init(&dev, 128, 64);
    ssd1306_clear_screen(&dev, false);
    ssd1306_contrast(&dev, 0xff);

    //I2C slave np532
    i2c_device_config_t pn532_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PN532_ADDRESS,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &pn532_cfg, &pn532_handle));


    //ENCODER
    pcnt_unit_config_t unit_config = {
        .high_limit = ENCODER_MAX_IDX,
        .low_limit = ENCODER_MIN_IDX,
        .flags.accum_count = true,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = ENCODER_GLITCH_FILTER_NS,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = ENCODER_CLK_GPIO,
        .level_gpio_num = ENCODER_DT_GPIO,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = ENCODER_DT_GPIO,
        .level_gpio_num = ENCODER_CLK_GPIO,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));
    ESP_ERROR_CHECK(
        pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE
        ));
    ESP_ERROR_CHECK(
        pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    ESP_ERROR_CHECK(
        pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE
        ));
    ESP_ERROR_CHECK(
        pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    //ENCODER BUTTON
    esp_rom_gpio_pad_select_gpio(ENCODER_SW_GPIO);
    gpio_set_direction(ENCODER_SW_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(ENCODER_SW_GPIO, GPIO_FLOATING);
    gpio_set_intr_type(ENCODER_SW_GPIO, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    gpio_isr_handler_add(ENCODER_SW_GPIO, gpio_isr_handler, NULL);


    //GPIO 33 accum v
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, BATTERY_ADC, &config));
}

uint8_t tick = 0;
uint8_t upd_count = 0;

void app_main(void)
{
    init();
    //MAIN LOOP
    while (true)
    {
        if (tick++ != 0) continue;

        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, BATTERY_ADC, &adc_raw));
        adc_raw = adc_raw >> 9;
        if (adc_raw != prev_adc_raw)
        {
            state_changed |= 0b00000001;
            prev_adc_raw = adc_raw;
        }

        ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
        int diff = (pulse_count - prev_pulse_count) / 2;
        if (diff != 0)
        {
            if (menu)
            {
                if (pulse_count == 0 && diff / 20 > 1)
                    diff = diff < 0 ? -1 : 1;
                selected_menu += diff;
                if (selected_menu > 3)
                    selected_menu = 0;
                else if (selected_menu < 0)
                    selected_menu = 3;
                state_changed |= 0b00000100;
            }
            else
            {
                if (pulse_count == 0 && diff / 20 > 1)
                    diff = diff < 0 ? -1 : 1;
                selected_num += diff;
                if (selected_num > 20)
                    selected_num = 0;
                else if (selected_num < 0)
                    selected_num = 20;
                state_changed |= 0b00000010;
            }
            prev_pulse_count = pulse_count;
        }


        if (state_changed != 0)
        {
            if (upd_count++ % 32 == 0)
                ssd1306_clear_screen(&dev, false);

            display_static_content();
            display_acb();
            display_selected_number();
            display_selected_menu();

            state_changed = 0;
        }
    }
}

void display_acb()
{
    for (int y = 1; y < 6; y++)
    {
        hud_acb[y*4] &= 0x80;
        for (int x = 1; x < 3; x++)
            hud_acb[y * 4 + x] = 0x00;
        hud_acb[y*4+3] &= 0x03;
    }
    uint8_t map[] = {0x80, 0x00, 0x00, 0x02};
    int c = adc_raw * 7 / 2 + 1;
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
    ssd1306_bitmaps(&dev, 0, 0, hud_acb, 32, 7, false);
}

void display_static_content()
{
    ssd1306_bitmaps(&dev, 0, 14, hud_button_read, 40, 9, false);
    ssd1306_bitmaps(&dev, 0, 24, hud_button_write, 40, 9, false);
    ssd1306_bitmaps(&dev, 0, 34, hud_button_use, 40, 9, false);
    ssd1306_bitmaps(&dev, 0, 44, hud_button_exit, 40, 9, false);
    ssd1306_bitmaps(&dev, 76, 15, hud_slash, 16, 34, false);
    ssd1306_bitmaps(&dev, 92, 15, seg2, 16, 34, false);
    ssd1306_bitmaps(&dev, 108, 15, seg0, 16, 34, false);
}

void display_selected_number()
{
    if (selected_num > 9)
    {
        if (selected_num > 19)
            ssd1306_bitmaps(&dev, 45, 15, seg2, 16, 34, false);
        else
            ssd1306_bitmaps(&dev, 45, 15, seg1, 16, 34, false);
    }
    else
    {
        ssd1306_bitmaps(&dev, 45, 15, seg0, 16, 34, false);
    }
    switch (selected_num % 10)
    {
    case 1:
        ssd1306_bitmaps(&dev, 61, 15, seg1, 16, 34, false);
        break;
    case 2:
        ssd1306_bitmaps(&dev, 61, 15, seg2, 16, 34, false);
        break;
    case 3:
        ssd1306_bitmaps(&dev, 61, 15, seg3, 16, 34, false);
        break;
    case 4:
        ssd1306_bitmaps(&dev, 61, 15, seg4, 16, 34, false);
        break;
    case 5:
        ssd1306_bitmaps(&dev, 61, 15, seg5, 16, 34, false);
        break;
    case 6:
        ssd1306_bitmaps(&dev, 61, 15, seg6, 16, 34, false);
        break;
    case 7:
        ssd1306_bitmaps(&dev, 61, 15, seg7, 16, 34, false);
        break;
    case 8:
        ssd1306_bitmaps(&dev, 61, 15, seg8, 16, 34, false);
        break;
    case 9:
        ssd1306_bitmaps(&dev, 61, 15, seg9, 16, 34, false);
        break;
    default:
        ssd1306_bitmaps(&dev, 61, 15, seg0, 16, 34, false);
    }
}

void display_selected_menu()
{
    switch (selected_menu)
    {
    case 0:
        ssd1306_bitmaps(&dev, 0, 14, hud_button_read, 40, 9, true);
        break;
    case 1:
        ssd1306_bitmaps(&dev, 0, 24, hud_button_write, 40, 9, true);
        break;
    case 2:
        ssd1306_bitmaps(&dev, 0, 34, hud_button_use, 40, 9, true);
        break;
    case 3:
        ssd1306_bitmaps(&dev, 0, 44, hud_button_exit, 40, 9, true);
        break;
    default: break;
    }
}
