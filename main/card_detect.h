//
// Created by s1pepega on 23.11.2026.
//

#ifndef CWS5Y26_CARD_DETECT_H
#define CWS5Y26_CARD_DETECT_H
#include <stdint.h>

#include "esp_err.h"
#include "pn532.h"
#include "core.h"

#endif //CWS5Y26_CARD_DETECT_H

typedef enum {
    CARD_TYPE_UNKNOWN = 0,
    // Базовые типы (определяются по SAK/ATQA)
    CARD_TYPE_MIFARE_CLASSIC_1K,
    CARD_TYPE_MIFARE_CLASSIC_4K,
    CARD_TYPE_MIFARE_CLASSIC_MINI,
    CARD_TYPE_MIFARE_ULTRALIGHT,
    CARD_TYPE_MIFARE_ULTRALIGHT_C,
    CARD_TYPE_NTAG_213,
    CARD_TYPE_NTAG_215,
    CARD_TYPE_NTAG_216,
    CARD_TYPE_MIFARE_DESFIRE,
    // «Магические» поколения (определяются активными пробами)
    CARD_TYPE_MAGIC_GEN1A,
    CARD_TYPE_MAGIC_GEN2,
    CARD_TYPE_MAGIC_GEN3,
    CARD_TYPE_MAGIC_GEN4,
} card_type_t;

typedef struct {
    card_type_t type;
    uint16_t    block_count;   // 64/256 для Classic, кол-во страниц для UL/NTAG
    uint8_t     block_size;    // 16 для Classic, 4 для UL/NTAG
    uint8_t     uid[10];
    uint8_t     uid_len;
    uint8_t     sak;
    uint8_t     atqa[2];
    bool        is_magic;      // true для Gen1a/Gen2/Gen3/Gen4
    bool        uid_writable;  // true, если UID можно перезаписать
} card_info_t;

const char* card_type_to_string(card_type_t type);
esp_err_t detect_card_type(pn532_t* dev, card_info_t* info);
esp_err_t detect_gen2_by_write_test(pn532_t* dev, card_info_t* info);