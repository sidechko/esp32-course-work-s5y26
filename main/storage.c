//
// Created by s1pepega on 22.11.2026.
//
#include "storage.h"

#include "esp_log.h"
#define NVS_NAMESPACE "course_work"

void init_nvs()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

esp_err_t save_mifare_card_to_nvs(mifare_card_data_handle_t card_h, const char *key) {
    if (card_h == NULL || card_h->block_data == NULL || key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    // 1. Сохраняем количество блоков (метаданные)
    char count_key[32];
    snprintf(count_key, sizeof(count_key), "%s_count", key);
    err = nvs_set_u16(nvs_handle, count_key, card_h->block_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save block count: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // 2. Сохраняем сами данные блоков как blob
    size_t data_size = card_h->block_count * MIFARE_BLOCK_SIZE;
    char data_key[32];
    snprintf(data_key, sizeof(data_key), "%s_data", key);
    err = nvs_set_blob(nvs_handle, data_key, card_h->block_data, data_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save block data: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Фиксируем изменения на флеш-памяти
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Card data saved to NVS with key '%s'", key);
    }
    return err;
}

mifare_card_data_handle_t load_mifare_card_from_nvs(const char *key) {
    if (key == NULL) return NULL;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return NULL;
    }

    // 1. Читаем количество блоков
    char count_key[32];
    snprintf(count_key, sizeof(count_key), "%s_count", key);
    uint16_t block_count = 0;
    err = nvs_get_u16(nvs_handle, count_key, &block_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read block count: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return NULL;
    }

    // 2. Выделяем память под структуру и данные
    mifare_card_data_handle_t card_h = malloc(sizeof(mifare_card_data_t));
    if (card_h == NULL) {
        nvs_close(nvs_handle);
        return NULL;
    }
    card_h->block_count = block_count;
    card_h->block_data = malloc(block_count * MIFARE_BLOCK_SIZE);
    if (card_h->block_data == NULL) {
        free(card_h);
        nvs_close(nvs_handle);
        return NULL;
    }

    // 3. Читаем blob с данными блоков
    char data_key[32];
    snprintf(data_key, sizeof(data_key), "%s_data", key);
    size_t required_size = block_count * MIFARE_BLOCK_SIZE;
    err = nvs_get_blob(nvs_handle, data_key, card_h->block_data, &required_size);
    nvs_close(nvs_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read block data: %s", esp_err_to_name(err));
        free(card_h->block_data);
        free(card_h);
        return NULL;
    }

    ESP_LOGI(TAG, "Card data loaded from NVS with key '%s' (%u blocks)", key, block_count);
    return card_h;
}

