//
// Created by s1pepega on 21.11.2026.
//

#ifndef CWS5Y26_PN532_H
#define CWS5Y26_PN532_H
#include "pn532_driver.h"
#include "hal/i2c_types.h"
#include "pn532.h"

#endif //CWS5Y26_PN532_H

#define TAG "CW_PN532"
#define MIFARE_BLOCK_SIZE 16
#define UID_SIZE 7

struct mifare_card_data;
typedef struct mifare_card_data mifare_card_data_t;
typedef struct mifare_card_data * mifare_card_data_handle_t;

struct mifare_card_data
{
    uint16_t block_count;
    uint8_t* block_data;
};

void cw_init_i2c_pn_532(pn532_io_t* dev, i2c_port_t port);
mifare_card_data_handle_t read_mifare_classic(pn532_io_t* dev);
esp_err_t get_mifare_classic_block_count(pn532_io_t* dev, int* count);
esp_err_t read_mifare_classic_block(pn532_io_t* dev, mifare_card_data_handle_t card_h, uint8_t block_number, int32_t timeout);
esp_err_t write_mifare_classic_block(pn532_io_t* dev, const uint8_t* data_to_write, uint8_t block_number, int32_t timeout);
