#include "cw_pn532.h"


static esp_err_t select_and_auth(pn532_t* dev, uint8_t block_number, uint8_t* uid_out) {
    pn532_poll_status_t status;
    pn532_uids_array_t* uids = pn532_14443_get_all_uids_ex(dev, &status);
    if (status != PN532_POLL_FOUND || uids == nullptr || uids->uids_count == 0) {
        free(uids);
        return ESP_FAIL;
    }

    pn532_uid_t* uid = &uids->uids[0];
    if (!pn532_14443_select_by_uid(dev, uid)) {
        free(uids);
        return ESP_FAIL;
    }

    // Сохраняем UID, если нужно
    if (uid_out) {
        memcpy(uid_out, uid->uid, uid->uid_length);
    }

    // Аутентификация ключом A (0x60). Ключ по умолчанию FF FF FF FF FF FF.
    uint8_t key_a[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (!pn532_14443_authenticate(dev, key_a, 0x60, uid, block_number)) {
        pn532_release_target(dev);
        free(uids);
        return ESP_FAIL;
    }

    free(uids);
    return ESP_OK;
}

pn532_dev_handle_t cw_init_i2c_pn_532(i2c_port_t port, pn532_bus_t** bus_out, uint16_t address) {
    // Инициализация шины I2C. Адрес 0 означает использование адреса по умолчанию (0x24).
    // Параметры SCL/SDA (GPIO_NUM_22, GPIO_NUM_21) замените на ваши.
    pn532_bus_t* bus = pn532_i2c_attach(port, address, 0);
    if (bus == nullptr) {
        ESP_LOGE(TAG, "Failed to attach PN532 to the existing I2C bus");
        return nullptr;
    }

    pn532_dev_handle_t dev = pn532_init(bus, GPIO_NUM_NC, GPIO_NUM_NC);
    if (dev == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize PN532 device");
        pn532_bus_destroy(bus);
        return nullptr;
    }

    *bus_out = bus; // Сохраняем указатель на шину для последующего освобождения
    ESP_LOGI(TAG, "PN532 initialized successfully over I2C");
    return dev;
}

esp_err_t get_mifare_classic_block_count(pn532_dev_handle_t dev, int* count) {
    esp_err_t err = read_mifare_classic_block(dev, nullptr, 255, 1000);
    if (err == ESP_OK) {
        *count = 256;
        return ESP_OK;
    }
    err = read_mifare_classic_block(dev, nullptr, 63, 1000);
    if (err == ESP_OK) {
        *count = 64;
        return ESP_OK;
    }
    *count = 0;
    return err;
}

esp_err_t read_mifare_classic_block(pn532_dev_handle_t dev, mifare_card_data_handle_t card_h,
                                    uint8_t block_number, int32_t timeout) {
    uint8_t uid[UID_SIZE];
    esp_err_t err = select_and_auth(dev, block_number, uid);
    if (err != ESP_OK) return err;

    uint8_t block_data[MIFARE_BLOCK_SIZE];
    if (!pn532_14443_block_read(dev, block_number, block_data, sizeof(block_data))) {
        ESP_LOGE(TAG, "Failed to read block %d", block_number);
        pn532_release_target(dev);
        return ESP_FAIL;
    }

    if (card_h != nullptr) {
        memcpy(card_h->block_data + block_number * MIFARE_BLOCK_SIZE, block_data, MIFARE_BLOCK_SIZE);
    }

    pn532_release_target(dev);
    return ESP_OK;
}

mifare_card_data_handle_t read_mifare_classic(pn532_dev_handle_t dev) {
    mifare_card_data_handle_t new_card = malloc(sizeof(mifare_card_data_t));
    if (new_card == nullptr) return nullptr;

    int count;
    esp_err_t err = get_mifare_classic_block_count(dev, &count);
    if (count == 0) {
        ESP_LOGE(TAG, "Failed to determine card capacity: %s", esp_err_to_name(err));
        free(new_card);
        return nullptr;
    }

    new_card->block_count = count;
    new_card->block_data = malloc(count * MIFARE_BLOCK_SIZE);
    if (new_card->block_data == nullptr) {
        free(new_card);
        return nullptr;
    }

    for (int i = 0; i < count; i++) {
        err = read_mifare_classic_block(dev, new_card, i, 1000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Read failed at block %d: %s", i, esp_err_to_name(err));
            free(new_card->block_data);
            free(new_card);
            return nullptr;
        }
    }
    return new_card;
}

esp_err_t write_mifare_classic_block(pn532_dev_handle_t dev, const uint8_t* data_to_write,
                                     uint8_t block_number, int32_t timeout) {
    esp_err_t err = select_and_auth(dev, block_number, nullptr);
    if (err != ESP_OK) return err;

    if (!pn532_14443_block_write(dev, block_number, data_to_write, MIFARE_BLOCK_SIZE)) {
        ESP_LOGE(TAG, "Failed to write block %d", block_number);
        pn532_release_target(dev);
        return ESP_FAIL;
    }

    // Небольшая задержка, чтобы карта успела сохранить данные
    vTaskDelay(pdMS_TO_TICKS(10));
    pn532_release_target(dev);
    ESP_LOGI(TAG,"Card read finished.");
    return ESP_OK;
}

esp_err_t write_mifare_classic(pn532_t* dev, mifare_card_data_handle_t card_h) {
    if (card_h == nullptr || card_h->block_data == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    // --- Шаг 1: Попытка разблокировать карту как Gen1a ---
    ESP_LOGI(TAG, "Attempting Gen1a unlock sequence before writing UID...");
    esp_err_t unlock_err = pn532_gen1a_unlock(dev);

    if (unlock_err == ESP_OK) {
        ESP_LOGI(TAG, "Gen1a unlock successful. Card is likely a Gen1a magic card.");
    } else {
        ESP_LOGW(TAG, "Gen1a unlock failed or card is not Gen1a. "
                      "Will try standard write for block 0 (Gen2/CUID).");
    }

    // --- Шаг 2: Попытка записи блока 0 (UID) ---
    // Если карта Gen1a и была разблокирована, запись пройдёт.
    // Если карта Gen2 (CUID), запись тоже пройдёт стандартным образом.
    const uint8_t* block0_data = card_h->block_data;
    esp_err_t err = write_mifare_classic_block(dev, block0_data, 0, 1000);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Block 0 (UID) written successfully. Target was a magic card (Gen1a or Gen2).");
    } else {
        ESP_LOGW(TAG, "Failed to write block 0 (UID): %s. "
                      "Target is likely a normal (non-magic) card. Skipping UID clone.",
                 esp_err_to_name(err));
        // Не возвращаем ошибку, а продолжаем, чтобы записать хотя бы данные.
    }

    // --- Шаг 3: Запись остальных блоков (пропуская трейлеры секторов) ---
    for (uint16_t block = 1; block < card_h->block_count; block++) {
        if ((block % 4 == 3) && (block < 128)) {
            ESP_LOGW(TAG, "Skipping sector trailer block %d", block);
            continue;
        }

        const uint8_t* block_data = card_h->block_data + (block * MIFARE_BLOCK_SIZE);
        err = write_mifare_classic_block(dev, block_data, (uint8_t)block, 1000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Write failed at block %d: %s", block, esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "Block %d written", block);
    }

    return ESP_OK;
}

esp_err_t pn532_gen1a_unlock(pn532_t* dev) {
    uint8_t response[16];
    size_t response_len;
    bool success;

    // --- Шаг 1: Отправка 0x40 как 7-битного кадра ---
    ESP_LOGI(TAG, "Gen1a unlock: sending 0x40 (7-bit frame)...");

    // Параметры команды InCommunicateThru (0x42):
    //   0x01 — Tg (Target number)
    //   0x40 — DataOut (байт «бэкдора»)
    //   0x07 — NbBits: последний байт содержит 7 значащих бит
    const uint8_t params_40[] = { 0x01, 0x40, 0x07 };

    response_len = sizeof(response);
    success = pn532_execute_command(dev, 0x42, params_40, sizeof(params_40),
                                    response, &response_len, 1000);
    if (!success) {
        ESP_LOGE(TAG, "No response to 7-bit 0x40 command");
        return ESP_FAIL;
    }

    // Логируем сырой ответ для отладки
    ESP_LOGI(TAG, "0x40 response: len=%u, first byte=0x%02X",
             (unsigned)response_len, response_len > 0 ? response[0] : 0);

    // Gen1a обычно отвечает ACK (0x0A). Проверяем хотя бы наличие ответа.
    if (response_len == 0 || (response[0] != 0x0A && response[0] != 0x00)) {
        ESP_LOGW(TAG, "Card did not ACK the 7-bit 0x40 command");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Card ACKed 0x40. Likely a Gen1a magic card.");

    // --- Шаг 2: Отправка 0x43 как обычного 8-битного кадра ---
    ESP_LOGI(TAG, "Gen1a unlock: sending 0x43 (8-bit frame)...");

    // Для 8-битного кадра NbBits можно не указывать — только Tg + DataOut.
    const uint8_t params_43[] = { 0x01, 0x43 };

    response_len = sizeof(response);
    success = pn532_execute_command(dev, 0x42, params_43, sizeof(params_43),
                                    response, &response_len, 1000);
    if (!success) {
        ESP_LOGE(TAG, "No response to 0x43 command");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Gen1a unlock sequence completed. Block 0 should now be writable.");
    return ESP_OK;
}
