//
// Created by s1pepega on 21.11.2026.
//

#ifndef CWS5Y26_STORAGE_H
#define CWS5Y26_STORAGE_H

#endif //CWS5Y26_STORAGE_H

#include "core.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"

void init_nvs();
esp_err_t save_mifare_card_to_nvs(mifare_card_data_handle_t card_h, const char *key);
mifare_card_data_handle_t load_mifare_card_from_nvs(const char *key);
