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
        err = read_mifare_classic_block(dev, new_card, i, 100);
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

    if (pn532_14443_block_write(dev, block_number, data_to_write, MIFARE_BLOCK_SIZE)) {
        ESP_LOGE(TAG, "Failed to write block %d", block_number);
        pn532_release_target(dev);
        return ESP_FAIL;
    }

    // Небольшая задержка, чтобы карта успела сохранить данные
    vTaskDelay(pdMS_TO_TICKS(20));
    pn532_release_target(dev);
    ESP_LOGI(TAG,"Card write finished.");
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
    esp_err_t err = write_mifare_classic_block(dev, block0_data, 0, 100);

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

esp_err_t emulate_mifare_card(pn532_t* dev, mifare_card_data_handle_t card_h) {
    if (dev == NULL || card_h == NULL || card_h->block_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 1. Подготовка параметров TgInitAsTarget для Mifare Classic (PICC mode)
    //    Структура кадра: D4 8C <Mode> <MifareParams (6 байт)> <FeliCaParams (18 байт)> ...
    //    Для Mifare Classic используется только MifareParams: SENS_RES (2 байта), SEL_RES (1 байт), UID (4 байта).
    //    SENS_RES для Mifare Classic 1K: 0x00 0x04 (ATQA)
    //    SEL_RES: 0x08 (SAK)
    //    UID берётся из первых 4 байт блока 0 ( card_h->block_data[0..3] )
    const uint8_t sens_res[2] = {0x00, 0x04};
    const uint8_t sel_res = 0x08; // SAK для Mifare Classic 1K
    const uint8_t* uid = card_h->block_data; // UID хранится в первых 4 байтах блока 0

    // Формируем параметры для TgInitAsTarget
    // Mode = 0x00 (PICC mode, passive)
    // MifareParams = SENS_RES[0] SENS_RES[1] SEL_RES UID[0] UID[1] UID[2] UID[3]
    // Остальные параметры (FeliCa, NFCID3, Length, etc.) заполняются нулями или опускаются.
    uint8_t tg_init_params[6 + 18 + 1 + 1 + 1] = {0}; // Упрощённый буфер
    size_t param_idx = 0;

    // Mode
    tg_init_params[param_idx++] = 0x00; // PICC only, passive

    // MifareParams (6 байт)
    tg_init_params[param_idx++] = sens_res[0];
    tg_init_params[param_idx++] = sens_res[1];
    tg_init_params[param_idx++] = sel_res;
    tg_init_params[param_idx++] = uid[0];
    tg_init_params[param_idx++] = uid[1];
    tg_init_params[param_idx++] = uid[2];
    tg_init_params[param_idx++] = uid[3];
    // FeliCaParams (18 байт) — оставляем нулями, так как эмулируем Mifare

    // 2. Отправка команды TgInitAsTarget (0x8C)
    uint8_t response[32];
    size_t response_len = sizeof(response);
    bool success = pn532_execute_command(dev, 0x8C, tg_init_params, param_idx, response, &response_len, 1000);
    if (!success) {
        ESP_LOGE(TAG, "TgInitAsTarget failed. Card emulation not started.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Card emulation started (UID: %02X%02X%02X%02X)", uid[3], uid[2], uid[1], uid[0]);

    // 3. Ожидание и обработка запросов от считывателя (TgGetData / TgSetData)
    //    В реальном сценарии здесь нужно циклически вызывать TgGetData, анализировать команды
    //    (например, READ 0x30 <block>), и отвечать через TgSetData.
    //    Для краткости приведён только каркас.
    for (int i = 0; i < 10; i++) { // Ограничим цикл, чтобы не блокировать навсегда
        uint8_t tg_data_cmd = 0x86; // TgGetData
        uint8_t tg_data_response[64];
        size_t tg_data_len = sizeof(tg_data_response);
        success = pn532_execute_command(dev, tg_data_cmd, NULL, 0, tg_data_response, &tg_data_len, 1000);

        if (success && tg_data_len > 0) {
            // Анализ полученной команды от считывателя
            // Например, если пришла команда READ (0x30) с номером блока,
            // можно попытаться отправить данные через TgSetData (0x8E).
            // Это упрощённая логика; в реальности требуется полноценный парсер ISO14443-3.
            ESP_LOGI(TAG, "Received data from reader (len=%u)", tg_data_len);

            // Пример: если команда — READ (0x30) блока 0, отправляем блок 0
            if (tg_data_response[0] == 0x30) {
                uint8_t block_num = tg_data_response[1];
                if (block_num < card_h->block_count) {
                    const uint8_t* block_data = card_h->block_data + (block_num * MIFARE_BLOCK_SIZE);
                    // Формируем ответ TgSetData: 0x8E <DataOut>
                    uint8_t set_data_params[1 + MIFARE_BLOCK_SIZE];
                    set_data_params[0] = 0x00; // Tg (target number)
                    memcpy(&set_data_params[1], block_data, MIFARE_BLOCK_SIZE);
                    size_t set_resp_len = sizeof(response);
                    pn532_execute_command(dev, 0x8E, set_data_params, sizeof(set_data_params), response, &set_resp_len, 1000);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // 4. Завершение эмуляции (опционально)
    //    Можно отправить TgInitAsTarget с Mode = 0x02 (release) или просто сбросить поле.
    ESP_LOGI(TAG, "Card emulation stopped.");
    return ESP_OK;
}
