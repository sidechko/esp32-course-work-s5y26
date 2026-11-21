#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"

#include "ssd1306.h"
#include "display_content.h"

#include "pn532.h"
#include "pn532_driver_i2c.h"

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

#define BATTERY_GPIO 32
#define BATTERY_ADC ADC_CHANNEL_4

#define I2C_TICKS_TO_WAIT 500

#define MAX_UID_LEN 10

//DEVICES
pcnt_unit_handle_t pcnt_unit;
adc_oneshot_unit_handle_t adc1_handle;

i2c_master_bus_handle_t bus_handle;
// i2c_master_dev_handle_t pn532_handle;
SSD1306_t dev;
pn532_io_t pn532_io;

#define BUTTON_CLICK_BIT 16
#define ACB_UPDATE_BIT 8
#define NUM_CHANGE_BIT 4
#define MENU_CHANGE_BIT 2
#define MENU_ACTIVE_BIT 1

// APP STATE
int prev_pulse_count = 0;
int pulse_count = 0;
int adc_raw = 0;

// LOG STATE
int acb_seg_count = 0;
int selected_num = 0;
int selected_menu = 4;
/*
    0bBCDEF
    B - Menu click bit
    C - ACB state change bit
    D - Number change bit
    E - Menu selected entry change bit
    F - Menu change bit
*/
volatile uint8_t state_changed = 0b01110;

//function
void display_static_content();
void display_selected_number();
void display_selected_menu();
bool recalc_acb_seg();
void display_acb();
uint16_t get_mifare_classic_block_count();
esp_err_t read_mifare_classic_block(uint8_t block_number, uint8_t *out_data, uint8_t *out_uid, int32_t timeout);

#define MIN_INTERVAL_US 200000
static volatile long last_isr_time = 0;
static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    long now = esp_timer_get_time();
    if (now - last_isr_time < MIN_INTERVAL_US)
        return;
    if(state_changed & BUTTON_CLICK_BIT)
        return;
    last_isr_time = now;
    state_changed |= BUTTON_CLICK_BIT;
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

    //I2C slave pn532

    ESP_ERROR_CHECK(pn532_new_driver_i2c(GPIO_NUM_NC, GPIO_NUM_NC, -1, -1, I2C_NUM_0, &pn532_io));
    esp_err_t err;
    do {
        err = pn532_init(&pn532_io);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "failed to initialize PN532");
            pn532_release(&pn532_io);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    } while(err != ESP_OK);

    ESP_LOGI(TAG, "get firmware version");
    uint32_t version_data = 0;
    do {
        err = pn532_get_firmware_version(&pn532_io, &version_data);
        if (ESP_OK != err) {
            ESP_LOGI(TAG, "Didn't find PN53x board");
            pn532_reset(&pn532_io);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    } while (ESP_OK != err);

    // Log firmware infos
    ESP_LOGI(TAG, "Found chip PN5%x", (unsigned int)(version_data >> 24) & 0xFF);
    ESP_LOGI(TAG, "Firmware ver. %d.%d", (int)(version_data >> 16) & 0xFF, (int)(version_data >> 8) & 0xFF);

    ESP_ERROR_CHECK(pn532_init(&pn532_io));


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

    esp_log_level_set("_ssd1306_bitmaps", ESP_LOG_INFO);
    esp_log_level_set("ssd1306_copy_bit", ESP_LOG_INFO);
    esp_log_level_set("PN532", ESP_LOG_WARN);
    
}

uint8_t tick = 0;
uint8_t upd_count = 0;


#define MIFARE_BLOCK_SIZE 16
#define UID_SIZE 7
uint8_t block_data[MIFARE_BLOCK_SIZE];
uint8_t curr_uid[UID_SIZE];

void app_main(void)
{
    init();
    //MAIN LOOP
    display_static_content();
    while (true)
    {
        if (tick++ != 0) continue;

        //INPUT
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, BATTERY_ADC, &adc_raw));
        if(recalc_acb_seg())
            state_changed |= ACB_UPDATE_BIT;
        vTaskDelay(pdMS_TO_TICKS(10));

        ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &pulse_count));
        int diff = (pulse_count - prev_pulse_count) / 2;
        if (diff != 0)
        {
            if (state_changed & MENU_ACTIVE_BIT)
            {
                if (pulse_count == 0 && diff / 20 > 1)
                    diff = diff < 0 ? -1 : 1;
                selected_menu += diff;
                if (selected_menu > 3)
                    selected_menu = 0;
                else if (selected_menu < 0)
                    selected_menu = 3;
                state_changed |= MENU_CHANGE_BIT;
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
                state_changed |= NUM_CHANGE_BIT;
            }
            prev_pulse_count = pulse_count;
        }
        
        //WORK
        if(state_changed & BUTTON_CLICK_BIT){

            if(state_changed & MENU_ACTIVE_BIT){
                switch(selected_menu){
                    case 0:
                        int32_t count = get_mifare_classic_block_count();
                        ESP_LOGI(TAG, "Count: %d", count);
                        for(int i = 0; i < count; i++){
                            esp_err_t err = read_mifare_classic_block(i, block_data, curr_uid, 1000);
                            if (err == ESP_OK) {
                                if(i == 0)
                                    ESP_LOG_BUFFER_HEX("UUID", curr_uid, 7);
                                ESP_LOG_BUFFER_HEX("    ", block_data, MIFARE_BLOCK_SIZE);
                            } else {
                                ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(err));
                            }
                            vTaskDelay(pdMS_TO_TICKS(10));
                        }
                        break;
                    case 1:
                    case 2:
                    default:
                        break;
                }
                selected_menu = 4;
            }
            else{
                
                selected_menu = 0;
            }
            state_changed |= MENU_CHANGE_BIT;
            state_changed ^= MENU_ACTIVE_BIT;
            state_changed &= ~BUTTON_CLICK_BIT;
        }

        //DISPLAY
        if (state_changed & ACB_UPDATE_BIT){
            display_acb();
        }
        if (state_changed & NUM_CHANGE_BIT){
            display_selected_number();
        }
        if (state_changed & MENU_CHANGE_BIT){
            display_selected_menu();
        }


        state_changed &= (BUTTON_CLICK_BIT | MENU_ACTIVE_BIT);
    }
}

int prev_after_eval = 0;
bool recalc_acb_seg(){
    if(adc_raw < 1302)
        adc_raw = 1302;
    else if(adc_raw > 2562)
        adc_raw = 2562;
    if(prev_after_eval << 5 == adc_raw << 5)
        return false;
    prev_after_eval = adc_raw;
    int count = 29 - ((2562 - adc_raw) / 42);
    if(acb_seg_count != count){
        acb_seg_count = count;
        return true;
    }
    return false;
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
    ssd1306_bitmaps(&dev, 0, 0, hud_acb, 32, 7, false);
}

void display_static_content()
{
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
    ssd1306_bitmaps(&dev, 0, 14, hud_button_read, 40, 9, selected_menu == 0);
    ssd1306_bitmaps(&dev, 0, 24, hud_button_write, 40, 9, selected_menu == 1);
    ssd1306_bitmaps(&dev, 0, 34, hud_button_use, 40, 9, selected_menu == 2);
    ssd1306_bitmaps(&dev, 0, 44, hud_button_exit, 40, 9, selected_menu == 3);
}

uint16_t get_mifare_classic_block_count(void) {
    uint8_t dummy[16];
    uint8_t dummy_uid[7];

    esp_err_t err = read_mifare_classic_block(255, dummy, dummy_uid, 1000);
    if (err == ESP_OK) {
        return 256;
    }

    err = read_mifare_classic_block(63, dummy, dummy_uid, 1000);
    if (err == ESP_OK) {
        return 64;
    }

    return 0;
}

esp_err_t read_mifare_classic_block(uint8_t block_number, uint8_t *out_data, uint8_t *out_uid, int32_t timeout) {
    uint8_t uid[UID_SIZE];
    uint8_t uid_len;
    uint8_t response[32];
    uint8_t response_len;

    esp_err_t err = pn532_read_passive_target_id(&pn532_io, PN532_BRTY_ISO14443A_106KBPS, uid, &uid_len, timeout);
    if (err != ESP_OK) return err;

    // 2. Выбор карты
    err = pn532_in_list_passive_target(&pn532_io);
    if (err != ESP_OK) return err;

    // 3. UID для аутентификации (последние 4 байта для 7-байтных карт)
    const uint8_t *auth_uid = (uid_len == 7) ? &uid[3] : uid;

    // 4. Аутентификация сектора ключом A
    uint8_t key_a[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t auth_cmd[] = {
        MIFARE_CMD_AUTH_A, block_number,
        key_a[0], key_a[1], key_a[2], key_a[3], key_a[4], key_a[5],
        auth_uid[0], auth_uid[1], auth_uid[2], auth_uid[3]
    };
    response_len = sizeof(response);
    err = pn532_in_data_exchange(&pn532_io, auth_cmd, sizeof(auth_cmd), response, &response_len);
    if (err != ESP_OK) return err;

    // 5. Чтение блока
    uint8_t read_cmd[] = {MIFARE_CMD_READ, block_number};
    response_len = sizeof(response);
    err = pn532_in_data_exchange(&pn532_io, read_cmd, sizeof(read_cmd), response, &response_len);
    if (err != ESP_OK) return err;

    memcpy(out_data, response, MIFARE_BLOCK_SIZE);
    memcpy(out_uid, uid, UID_SIZE);
    return ESP_OK;
}
