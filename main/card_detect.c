//
// Created by s1pepega on 23.11.2026.
//
#include "card_detect.h"

static bool probe_gen4(pn532_t* dev);
static bool probe_gen1a(pn532_t* dev);
static bool probe_gen3(pn532_t* dev);
static esp_err_t classify_classic(pn532_t* dev, pn532_uid_t* uid, card_info_t* info);
static esp_err_t classify_ultralight(pn532_t* dev, pn532_uid_t* uid, card_info_t* info);
static esp_err_t read_classic_block_raw(pn532_t* dev, pn532_uid_t* uid, uint8_t block, uint8_t* out);

const char* card_type_to_string(card_type_t type) {
    switch (type) {
        case CARD_TYPE_MIFARE_CLASSIC_1K:   return "Mifare Classic 1K";
        case CARD_TYPE_MIFARE_CLASSIC_4K:   return "Mifare Classic 4K";
        case CARD_TYPE_MIFARE_CLASSIC_MINI: return "Mifare Classic Mini";
        case CARD_TYPE_MIFARE_ULTRALIGHT:   return "Mifare Ultralight";
        case CARD_TYPE_MIFARE_ULTRALIGHT_C: return "Mifare Ultralight C";
        case CARD_TYPE_NTAG_213:            return "NTAG213";
        case CARD_TYPE_NTAG_215:            return "NTAG215";
        case CARD_TYPE_NTAG_216:            return "NTAG216";
        case CARD_TYPE_MIFARE_DESFIRE:      return "Mifare DESFire";
        case CARD_TYPE_MAGIC_GEN1A:         return "Magic Gen1a";
        case CARD_TYPE_MAGIC_GEN2:          return "Magic Gen2 (CUID)";
        case CARD_TYPE_MAGIC_GEN3:          return "Magic Gen3";
        case CARD_TYPE_MAGIC_GEN4:          return "Magic Gen4 (Ultimate)";
        default:                            return "Unknown";
    }
}

esp_err_t detect_card_type(pn532_t* dev, card_info_t* info) {
    if (dev == NULL || info == NULL) return ESP_ERR_INVALID_ARG;
    memset(info, 0, sizeof(card_info_t));
    info->type = CARD_TYPE_UNKNOWN;

    // --- Шаг 1: обнаружение карты ---
    pn532_poll_status_t status;
    pn532_uids_array_t* uids = pn532_14443_get_all_uids_ex(dev, &status);
    if (status != PN532_POLL_FOUND || uids == NULL || uids->uids_count == 0) {
        ESP_LOGW(TAG, "No card in field");
        if (uids) free(uids);
        return ESP_FAIL;
    }
    pn532_uid_t* uid = &uids->uids[0];

    // --- Шаг 2: заполняем базовую информацию ---
    info->uid_len = uid->uid_length;
    memcpy(info->uid, uid->uid, uid->uid_length > sizeof(info->uid) ? sizeof(info->uid) : uid->uid_length);
    info->sak = uid->sak;
    // ATQA может быть uint8_t[2] или uint16_t — копируем 2 байта как есть
    memcpy(info->atqa, &uid->atqa, 2);

    ESP_LOGI(TAG, "Card: UID_len=%u SAK=0x%02X ATQA=0x%02X%02X",
             info->uid_len, info->sak, info->atqa[1], info->atqa[0]);

    // --- Шаг 3: выбор карты (обязательно перед активными пробами) ---
    if (!pn532_14443_select_by_uid(dev, uid)) {
        ESP_LOGE(TAG, "Failed to select card");
        free(uids);
        return ESP_FAIL;
    }

    // --- Шаг 4: классификация по SAK ---
    esp_err_t err = ESP_OK;

    if (info->sak & 0x08) {
        // Mifare Classic family
        err = classify_classic(dev, uid, info);
    } else if (info->sak == 0x00) {
        // Ultralight / NTAG family
        err = classify_ultralight(dev, uid, info);
    } else if (info->sak & 0x20) {
        // ISO14443-4 (DESFire и др.)
        info->type = CARD_TYPE_MIFARE_DESFIRE;
        info->block_size = 0;
        info->block_count = 0;
        ESP_LOGI(TAG, "ISO14443-4 card (likely DESFire)");
    } else {
        ESP_LOGW(TAG, "Unrecognized SAK 0x%02X", info->sak);
        info->type = CARD_TYPE_UNKNOWN;
    }

    free(uids);
    pn532_release_target(dev);
    return err;
}

esp_err_t detect_gen2_by_write_test(pn532_t* dev, card_info_t* info) {
    if (dev == NULL || info == NULL) return ESP_ERR_INVALID_ARG;
    if (info->type != CARD_TYPE_MIFARE_CLASSIC_1K &&
        info->type != CARD_TYPE_MIFARE_CLASSIC_4K) {
        return ESP_ERR_INVALID_STATE;
    }

    // Обнаружение и выбор карты
    pn532_poll_status_t status;
    pn532_uids_array_t* uids = pn532_14443_get_all_uids_ex(dev, &status);
    if (status != PN532_POLL_FOUND || !uids || uids->uids_count == 0) {
        if (uids) free(uids);
        return ESP_FAIL;
    }
    pn532_uid_t* uid = &uids->uids[0];
    if (!pn532_14443_select_by_uid(dev, uid)) {
        free(uids);
        return ESP_FAIL;
    }

    // Аутентификация в секторе 0
    uint8_t key_a[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (!pn532_14443_authenticate(dev, key_a, 0x60, uid, 0)) {
        ESP_LOGW(TAG, "Auth to sector 0 failed");
        free(uids);
        pn532_release_target(dev);
        return ESP_FAIL;
    }

    // Читаем блок 0, чтобы получить текущее содержимое
    uint8_t block0[16];
    if (!pn532_14443_block_read(dev, 0, block0, 16)) {
        ESP_LOGW(TAG, "Read block 0 failed");
        free(uids);
        pn532_release_target(dev);
        return ESP_FAIL;
    }

    // Пытаемся записать те же данные обратно — если это magic-карта, пройдёт
    bool wrote = pn532_14443_block_write(dev, 0, block0, 16);
    free(uids);
    pn532_release_target(dev);

    if (wrote) {
        ESP_LOGI(TAG, "Write-test succeeded — card is likely Gen2 (CUID)");
        info->type = CARD_TYPE_MAGIC_GEN2;
        info->is_magic = true;
        info->uid_writable = true;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Write-test failed — card is a regular Classic");
    return ESP_FAIL;
}

// ---- Внутренние функции ----

static esp_err_t classify_classic(pn532_t* dev, pn532_uid_t* uid, card_info_t* info) {
    info->block_size = 16;

    // Пытаемся определить объём: пробуем прочитать блок 255 (есть только у 4K)
    uint8_t scratch[16];
    if (read_classic_block_raw(dev, uid, 255, scratch) == ESP_OK) {
        info->block_count = 256;
        info->type = CARD_TYPE_MIFARE_CLASSIC_4K;
    } else if (read_classic_block_raw(dev, uid, 63, scratch) == ESP_OK) {
        info->block_count = 64;
        info->type = CARD_TYPE_MIFARE_CLASSIC_1K;
    } else if (read_classic_block_raw(dev, uid, 19, scratch) == ESP_OK) {
        // Mini имеет блоки 0..19 (5 секторов по 4 блока)
        info->block_count = 20;
        info->type = CARD_TYPE_MIFARE_CLASSIC_MINI;
    } else {
        // Не смогли прочитать — возможно, другой ключ. Пометим как 1K по умолчанию.
        info->block_count = 64;
        info->type = CARD_TYPE_MIFARE_CLASSIC_1K;
        ESP_LOGW(TAG, "Could not probe size; assuming Classic 1K");
    }

    ESP_LOGI(TAG, "Classic size: %u blocks -> %s",
             info->block_count, card_type_to_string(info->type));

    // --- Активные пробы «магических» поколений ---
    // Порядок: Gen4 → Gen1a → Gen3 → (если ничего) остаётся Classic
    if (probe_gen4(dev)) {
        info->type = CARD_TYPE_MAGIC_GEN4;
        info->is_magic = true;
        info->uid_writable = true;
        ESP_LOGI(TAG, "Detected Gen4 (Ultimate Magic)");
        return ESP_OK;
    }
    if (probe_gen1a(dev)) {
        info->type = CARD_TYPE_MAGIC_GEN1A;
        info->is_magic = true;
        info->uid_writable = true;
        ESP_LOGI(TAG, "Detected Gen1a");
        return ESP_OK;
    }
    if (probe_gen3(dev)) {
        info->type = CARD_TYPE_MAGIC_GEN3;
        info->is_magic = true;
        info->uid_writable = true;
        ESP_LOGI(TAG, "Detected Gen3 (APDU)");
        return ESP_OK;
    }

    // Не Gen1a/Gen3/Gen4 — обычная Classic или Gen2.
    // Точное различие требует записи в блок 0 (см. detect_gen2_by_write_test).
    info->is_magic = false;
    info->uid_writable = false;
    ESP_LOGI(TAG, "No magic backdoor detected (regular Classic or Gen2)");
    return ESP_OK;
}

static esp_err_t classify_ultralight(pn532_t* dev, pn532_uid_t* uid, card_info_t* info) {
    info->block_size = 4;

    // Пытаемся получить точный тип через GET_VERSION (Ultrlight EV1 / NTAG)
    // Вызов ниже пытается определить capacity и subtype через встроенную логику.
    // pn532_14443_detect_selected_card_type_and_capacity(dev, uid);

    // Попытка прочитать страницу 4 (пользовательская область Ultralight)
    uint8_t page[4];
    uint8_t params_read[] = { 0x01, 0x30, 0x04 };
    uint8_t resp[16];
    size_t resp_len = sizeof(resp);
    bool ok = pn532_execute_command(dev, 0x42, params_read, sizeof(params_read),
                                    resp, &resp_len, 1000);

    if (!ok || resp_len < 4) {
        // Не смогли прочитать — тип остаётся Ultralight по SAK
        info->type = CARD_TYPE_MIFARE_ULTRALIGHT;
        info->block_count = 16;
        ESP_LOGW(TAG, "GET_VERSION / read page 4 failed; assuming plain Ultralight");
        return ESP_OK;
    }

    // Пытаемся получить версию через APDU-подобную команду GET_VERSION (0x60)
    // Это поддерживают Ultralight EV1 и NTAG, но НЕ обычный Ultralight.
    uint8_t params_ver[] = { 0x01, 0x60 };
    resp_len = sizeof(resp);
    bool has_version = pn532_execute_command(dev, 0x42, params_ver, sizeof(params_ver),
                                             resp, &resp_len, 1000);

    if (!has_version || resp_len < 8) {
        // Ultralight без GET_VERSION — обычный UL или Ultralight C
        info->type = CARD_TYPE_MIFARE_ULTRALIGHT;
        info->block_count = 16;
        ESP_LOGI(TAG, "Plain Ultralight (16 pages)");
        return ESP_OK;
    }

    // Разбираем GET_VERSION: первый байт — код 0x00, второй — vendor ID (0x04 = NXP)
    // Байты 6..7 — размер памяти: (byte6 << 8) | byte7 (в байтах)
    //   NTAG213: 0x00 0x0F (144 байта = 36 страниц)
    //   NTAG215: 0x00 0x3E (504 байта = 126 страниц)
    //   NTAG216: 0x00 0x6D (888 байт = 222 страницы)
    //   Ultralight EV1: 0x00 0x0B (48) или 0x00 0x0E (128)
    uint16_t size_bytes = (uint16_t)((resp[6] << 8) | resp[7]);
    uint16_t pages = size_bytes / 4;

    info->block_count = pages;
    if (size_bytes >= 888) {
        info->type = CARD_TYPE_NTAG_216;
        info->block_count = 222;
    } else if (size_bytes >= 504) {
        info->type = CARD_TYPE_NTAG_215;
        info->block_count = 126;
    } else if (size_bytes >= 144) {
        info->type = CARD_TYPE_NTAG_213;
        info->block_count = 36;
    } else {
        info->type = CARD_TYPE_MIFARE_ULTRALIGHT;
        info->block_count = pages > 0 ? pages : 16;
    }

    ESP_LOGI(TAG, "Detected: %s (%u pages)",
             card_type_to_string(info->type), info->block_count);
    return ESP_OK;
}

// ---- Активные пробы ----

static bool probe_gen4(pn532_t* dev) {
    // Бэкдор-команда чтения блока 0: InCommunicateThru(0x42) с DataOut = CF <pass4> CE <block>
    const uint8_t params[] = { 0x01, 0xCF, 0x00, 0x00, 0x00, 0x00, 0xCE, 0x00 };
    uint8_t resp[32];
    size_t resp_len = sizeof(resp);
    if (!pn532_execute_command(dev, 0x42, params, sizeof(params), resp, &resp_len, 1000)) {
        return false;
    }
    // Ожидаем 16 байт данных блока 0 в ответ
    return resp_len == 16;
}

static bool probe_gen1a(pn532_t* dev) {
    // 7-битный кадр 0x40 через InCommunicateThru
    const uint8_t params[] = { 0x01, 0x40, 0x07 };
    uint8_t resp[16];
    size_t resp_len = sizeof(resp);
    if (!pn532_execute_command(dev, 0x42, params, sizeof(params), resp, &resp_len, 1000)) {
        return false;
    }
    return resp_len > 0 && resp[0] == 0x0A;
}

static bool probe_gen3(pn532_t* dev) {
    // Пробуем безопасную APDU-команду. Точная команда для идентификации Gen3
    // может отличаться от карты к карте; чаще всего Gen3 реагирует на формат
    // 90 xx xx xx xx, возвращая APDU-статус (SW1 в диапазоне 0x61..0x6F или 0x90).
    const uint8_t params[] = { 0x01, 0x90, 0x00, 0x00, 0x00, 0x00 };
    uint8_t resp[32];
    size_t resp_len = sizeof(resp);
    if (!pn532_execute_command(dev, 0x42, params, sizeof(params), resp, &resp_len, 1000)) {
        return false;
    }
    if (resp_len < 2) return false;
    // Последние два байта — SW1 SW2 в APDU-ответе
    uint8_t sw1 = resp[resp_len - 2];
    return (sw1 == 0x90) || (sw1 >= 0x61 && sw1 <= 0x6F);
}


static esp_err_t read_classic_block_raw(pn532_t* dev, pn532_uid_t* uid, uint8_t block, uint8_t* out) {
    // Аутентификация ключом A = FF FF FF FF FF FF
    uint8_t key_a[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (!pn532_14443_authenticate(dev, key_a, 0x60, uid, block)) {
        return ESP_FAIL;
    }

    // Чтение блока через стандартную команду READ (0x30) + номер блока
    const uint8_t params[] = { 0x01, 0x30, block };
    uint8_t resp[32];
    size_t resp_len = sizeof(resp);
    if (!pn532_execute_command(dev, 0x42, params, sizeof(params), resp, &resp_len, 1000)) {
        return ESP_FAIL;
    }
    if (resp_len < 16) return ESP_FAIL;
    memcpy(out, resp, 16);
    return ESP_OK;
}