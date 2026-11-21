#ifndef CWS5Y26_PN532_H
#define CWS5Y26_PN532_H

#include "pn532.h"
#include "esp_err.h"
#include "../core.h"

#define UID_SIZE 10 // Максимальный размер UID в PN532

typedef pn532_t* pn532_dev_handle_t;
typedef pn532_bus_t* pn532_bus_handle_t;


pn532_t* cw_init_i2c_pn_532(i2c_port_t port, pn532_bus_handle_t* bus_out, uint16_t address);

mifare_card_data_handle_t read_mifare_classic(pn532_dev_handle_t dev);
esp_err_t get_mifare_classic_block_count(pn532_dev_handle_t dev, int* count);

esp_err_t read_mifare_classic_block(pn532_dev_handle_t dev, mifare_card_data_handle_t card_h,
                                    uint8_t block_number, int32_t timeout);

esp_err_t write_mifare_classic_block(pn532_dev_handle_t dev, const uint8_t* data_to_write,
                                     uint8_t block_number, int32_t timeout);

esp_err_t write_mifare_classic(pn532_dev_handle_t dev, mifare_card_data_handle_t card_h);
esp_err_t pn532_gen1a_unlock(pn532_t* dev);

#endif // CWS5Y26_PN532_H
