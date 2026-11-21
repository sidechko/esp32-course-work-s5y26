#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "core.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "storage.h"

#include "parts/battery.h"
#include "parts/display.h"
#include "parts/encoder.h"
#include "parts/cw_pn532.h"


//APP CONST
#define ENCODER_CLK_GPIO 19
#define ENCODER_DT_GPIO 18
#define ENCODER_SW_GPIO 5
#define ENCODER_MIN_IDX (-40)
#define ENCODER_MAX_IDX 40
#define ENCODER_GLITCH_FILTER_NS 1000

#define MIN_INTERVAL_US 200000

#define I2C_SCL_GPIO 22
#define I2C_SDA_GPIO 21
#define I2C_MASTER_FREQ_HZ 400000

#define DISPLAY_ADDRESS 0x3c
#define PN532_ADDRESS 0x24


#define BATTERY_GPIO 32
#define BATTERY_ADC ADC_CHANNEL_4

//DEVICES
i2c_master_bus_handle_t bus_handle;
SSD1306_t display_dev;
pn532_dev_handle_t pn532_dev;
pn532_bus_handle_t pn532_bus_handle;
pcnt_unit_handle_t encoder_pcnt;
adc_oneshot_unit_handle_t battery_adc;

// APP STATE
state_t state_changed = 0b01110;

// encoder
int pulse_count = 0;
int prev_pulse_count = 0;

int selected_num = 0;
int selected_menu = 4;

//  encoder button
static volatile long last_isr_time = 0;

// adc
int adc_raw = 0;
int prev_after_eval = 0;

int acb_seg_count = 0;

void log_state(state_t* state)
{
    ESP_LOGI("DEBUG", "state: %d", *state);
    ESP_LOGI("DEBUG", "      ACTIVE : %d", *state & MENU_ACTIVE_BIT);
    ESP_LOGI("DEBUG", " MENU CHANGE : %d", *state & MENU_CHANGE_BIT);
    ESP_LOGI("DEBUG", "  NUM CHANGE : %d", *state & NUM_CHANGE_BIT);
    ESP_LOGI("DEBUG", "         ACB : %d", *state & ACB_UPDATE_BIT);
    ESP_LOGI("DEBUG", "       CLICK : %d", *state & BUTTON_CLICK_BIT);
}

void log_card(mifare_card_data_handle_t card_data)
{
    if (card_data == nullptr)
    {
        ESP_LOGI(TAG, "Card is nullptr");
        return;
    }

    uint16_t max = card_data->block_count;
    uint8_t* data_ptr = card_data->block_data;

    ESP_LOGI(TAG, "Card data");
    for (uint16_t i = 0; i < max; i++)
    {
        uint8_t block_data[MIFARE_BLOCK_SIZE];
        for (int j = 0; j < 16; ++j)
        {
            block_data[j] = *(data_ptr + MIFARE_BLOCK_SIZE * i + j);
        }
        ESP_LOG_BUFFER_HEX("    ", block_data, MIFARE_BLOCK_SIZE);
    }
}

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    long now = esp_timer_get_time();
    if (now - last_isr_time < MIN_INTERVAL_US)
        return;
    if (state_changed & BUTTON_CLICK_BIT)
        return;
    last_isr_time = now;
    state_changed |= BUTTON_CLICK_BIT;
}

void int_to_str(int value, char* buf, size_t buf_size)
{
    snprintf(buf, buf_size, "%d", value);
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
    init_i2c_display(bus_handle, &dev_cfg, DISPLAY_ADDRESS, &display_dev);
    //I2C slave pn532
    pn532_dev = cw_init_i2c_pn_532(I2C_NUM_0, &pn532_bus_handle,PN532_ADDRESS);
    //ENCODER
    init_encoder(&encoder_pcnt,ENCODER_MIN_IDX,ENCODER_MAX_IDX,ENCODER_GLITCH_FILTER_NS,ENCODER_CLK_GPIO,
                 ENCODER_DT_GPIO);
    //ENCODER BUTTON
    init_encoder_button(ENCODER_SW_GPIO, gpio_isr_handler);
    //BATTERY
    init_battery_oneshot(&battery_adc, ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_BITWIDTH_DEFAULT,BATTERY_ADC);
    //NVS
    init_nvs();
}

char* buffer;
mifare_card_data_handle_t curent_card;

void app_main(void)
{
    init();
    //MAIN LOOP
    display_static_content(&display_dev);
    while (true)
    {
        //INPUT
        one_shot_with_recalc(BATTERY_ADC, battery_adc, &adc_raw, &prev_after_eval, &acb_seg_count, &state_changed);
        vTaskDelay(pdMS_TO_TICKS(10));
        encoder_read(&encoder_pcnt, &pulse_count, &prev_pulse_count, &state_changed, &selected_menu, &selected_num);
        vTaskDelay(pdMS_TO_TICKS(10));

        //WORK
        if (state_changed & BUTTON_CLICK_BIT)
        {
            if (state_changed & MENU_ACTIVE_BIT)
            {
                int_to_str(selected_num, buffer, 2);
                esp_err_t err = ESP_FAIL;
                switch (selected_menu)
                {
                case 0:
                    ESP_LOGI(TAG, "Start reading");
                    curent_card = read_mifare_classic(pn532_dev);
                    if (curent_card != nullptr)
                    {
                        log_card(curent_card);
                        err = ESP_OK;
                    }
                    ESP_LOGI(TAG, "End reading");
                    break;
                case 1:
                    ESP_LOGI(TAG, "Start writing");
                    err = write_mifare_classic(pn532_dev, curent_card);
                    if (err != ESP_OK)
                        ESP_LOGI(TAG, "Error writing mifare_classic");
                    else
                        save_mifare_card_to_nvs(curent_card, buffer);
                    ESP_LOGI(TAG, "End writing");
                    break;
                case 2:
                    ESP_LOGI(TAG, "Start emulate");
                    curent_card = load_mifare_card_from_nvs(buffer);
                    err = emulate_mifare_card(pn532_dev,curent_card);
                    ESP_LOGI(TAG, "End emulate");
                default:
                    ESP_LOGI(TAG, "Exit, log current selected card");
                    curent_card = load_mifare_card_from_nvs(buffer);
                    log_card(curent_card);
                    break;
                }
                ESP_LOGE(TAG, "Status code: %s", esp_err_to_name(err));
                selected_menu = 4;
            }
            else
            {
                selected_menu = 0;
            }
            state_changed |= MENU_CHANGE_BIT;
            state_changed ^= MENU_ACTIVE_BIT;
            state_changed &= ~BUTTON_CLICK_BIT;
        }

        //DISPLAY
        display_dynamic_content(&display_dev, &state_changed, selected_num, selected_menu, acb_seg_count);
        state_changed &= (BUTTON_CLICK_BIT | MENU_ACTIVE_BIT);
    }
}
