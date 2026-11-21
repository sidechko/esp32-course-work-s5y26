//
// Created by s1pepega on 21.11.2026.
//
#include "cw_pn532.h"

#include "esp_log.h"
#include "pn532_driver_i2c.h"


void cw_init_i2c_pn_532(pn532_io_t* dev, i2c_port_t port)
{
    ESP_ERROR_CHECK(pn532_new_driver_i2c(GPIO_NUM_NC, GPIO_NUM_NC, -1, -1, port, dev));
    esp_err_t err;
    do
    {
        err = pn532_init(dev);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "failed to initialize PN532");
            pn532_release(dev);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
    while (err != ESP_OK);

    ESP_LOGI(TAG, "get firmware version");
    uint32_t version_data = 0;
    do
    {
        err = pn532_get_firmware_version(dev, &version_data);
        if (ESP_OK != err)
        {
            ESP_LOGI(TAG, "Didn't find PN53x board");
            pn532_reset(dev);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
    while (ESP_OK != err);

    // Log firmware infos
    ESP_LOGI(TAG, "Found chip PN5%x", (unsigned int)(version_data >> 24) & 0xFF);
    ESP_LOGI(TAG, "Firmware ver. %d.%d", (int)(version_data >> 16) & 0xFF, (int)(version_data >> 8) & 0xFF);

    ESP_ERROR_CHECK(pn532_init(dev));
    // Log firmware infos
    ESP_LOGI(TAG, "Found chip PN5%x", (unsigned int)(version_data >> 24) & 0xFF);
    ESP_LOGI(TAG, "Firmware ver. %d.%d", (int)(version_data >> 16) & 0xFF, (int)(version_data >> 8) & 0xFF);

    ESP_ERROR_CHECK(pn532_init(dev));
}

esp_err_t get_mifare_classic_block_count(pn532_io_t* dev, int* count)
{
    esp_err_t err = read_mifare_classic_block(dev, nullptr, 255, 1000);
    if (err == ESP_OK)
    {
        *count = 256;
        return err;
    }
    err = read_mifare_classic_block(dev, nullptr, 63, 1000);
    if (err == ESP_OK)
    {
        *count = 64;
        return err;
    }
    *count = 0;
    return err;
}

esp_err_t read_mifare_classic_block(pn532_io_t* dev, mifare_card_data_handle_t card_h, uint8_t block_number,
                                    int32_t timeout)
{
    uint8_t uid[UID_SIZE];
    uint8_t uid_len;
    uint8_t response[32];
    uint8_t response_len;

    esp_err_t err = pn532_read_passive_target_id(dev, PN532_BRTY_ISO14443A_106KBPS, uid, &uid_len, timeout);
    if (err != ESP_OK) return err;

    // 2. Выбор карты
    err = pn532_in_list_passive_target(dev);
    if (err != ESP_OK) return err;

    // 3. UID для аутентификации (последние 4 байта для 7-байтных карт)
    const uint8_t* auth_uid = (uid_len == 7) ? &uid[3] : uid;

    // 4. Аутентификация сектора ключом A
    uint8_t key_a[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t auth_cmd[] = {
        MIFARE_CMD_AUTH_A, block_number,
        key_a[0], key_a[1], key_a[2], key_a[3], key_a[4], key_a[5],
        auth_uid[0], auth_uid[1], auth_uid[2], auth_uid[3]
    };
    response_len = sizeof(response);
    err = pn532_in_data_exchange(dev, auth_cmd, sizeof(auth_cmd), response, &response_len);
    if (err != ESP_OK) return err;

    // 5. Чтение блока
    uint8_t read_cmd[] = {MIFARE_CMD_READ, block_number};
    response_len = sizeof(response);
    err = pn532_in_data_exchange(dev, read_cmd, sizeof(read_cmd), response, &response_len);
    if (err != ESP_OK) return err;

    if (card_h != nullptr)
        memcpy(card_h->block_data + block_number * MIFARE_BLOCK_SIZE, response, MIFARE_BLOCK_SIZE);

    return ESP_OK;
}
mifare_card_data_handle_t read_mifare_classic(pn532_io_t* dev)
{
    mifare_card_data_handle_t new_card = malloc(sizeof(mifare_card_data_t));
    int count;
    esp_err_t err = get_mifare_classic_block_count(dev, &count);
    if (count == 0)
    {
        ESP_LOGE(TAG, "Read failed at count", esp_err_to_name(err));
        free(new_card);
        return nullptr;
    }
    new_card->block_count = count;
    new_card->block_data = malloc(sizeof(uint8_t)*(count * MIFARE_BLOCK_SIZE));
    for(int i = 0; i < count; i++){
        err = read_mifare_classic_block(dev, new_card, i, 100);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(err));
            free(new_card->block_data);
            free(new_card);
            return nullptr;
            break;
        }
    }
    return new_card;
}
esp_err_t write_mifare_classic_block(pn532_io_t* dev, const uint8_t* data_to_write,
                                     uint8_t block_number, int32_t timeout)
{
    uint8_t uid[UID_SIZE];
    uint8_t uid_len;
    uint8_t response[32];
    uint8_t response_len;

    esp_err_t err = pn532_read_passive_target_id(dev, PN532_BRTY_ISO14443A_106KBPS, uid, &uid_len, timeout);
    if (err != ESP_OK) return err;

    err = pn532_in_list_passive_target(dev);
    if (err != ESP_OK) return err;

    // 2. Аутентификация сектора ключом A (как в функции чтения)
    const uint8_t* auth_uid = (uid_len == 7) ? &uid[3] : uid;
    uint8_t key_a[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t auth_cmd[] = {
        MIFARE_CMD_AUTH_A, block_number,
        key_a[0], key_a[1], key_a[2], key_a[3], key_a[4], key_a[5],
        auth_uid[0], auth_uid[1], auth_uid[2], auth_uid[3]
    };
    response_len = sizeof(response);
    err = pn532_in_data_exchange(dev, auth_cmd, sizeof(auth_cmd), response, &response_len);
    if (err != ESP_OK) return err;

    // 3. Команда записи: MIFARE_CMD_WRITE + номер блока + 16 байт данных
    uint8_t write_cmd[2 + MIFARE_BLOCK_SIZE];
    write_cmd[0] = MIFARE_CMD_WRITE;
    write_cmd[1] = block_number;
    memcpy(&write_cmd[2], data_to_write, MIFARE_BLOCK_SIZE);

    response_len = sizeof(response);
    err = pn532_in_data_exchange(dev, write_cmd, sizeof(write_cmd), response, &response_len);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(10));

    return ESP_OK;
}

esp_err_t write_mifare_classic(pn532_io_t* dev, mifare_card_data_handle_t card_h)
{
    if (card_h == NULL || card_h->block_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t* block0_data = card_h->block_data; // Блок 0 — это первые 16 байт
    esp_err_t err = write_mifare_classic_block(dev, block0_data, 0, 1000);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Block 0 (UID) written successfully. Target is likely a Gen2/CUID card.");
    } else {
        ESP_LOGW(TAG, "Failed to write block 0 (UID): %s. Target may be a normal card or Gen1a. "
                      "Skipping UID clone and continuing with data blocks.", esp_err_to_name(err));
    }

    for (uint16_t block = 1; block < card_h->block_count; block++) {
        // Пропускаем трейлеры секторов (3, 7, 11, ... для блоков < 128)
        if ((block % 4 == 3) && (block < 128)) {
            ESP_LOGW(TAG, "Skipping sector trailer block %d", block);
            continue;
        }

        const uint8_t* block_data = card_h->block_data + (block * MIFARE_BLOCK_SIZE);
        err = write_mifare_classic_block(dev, block_data, (uint8_t)block, 1000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Write failed at block %d: %s", block, esp_err_to_name(err));
            return err; // Прерываем, если не удалось записать важный блок данных
        }
        ESP_LOGI(TAG, "Block %d written", block);
    }

    return ESP_OK;
}


