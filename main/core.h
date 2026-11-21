//
// Created by s1pepega on 22.11.2026.
//
#pragma once
#define TAG "CW"
/*
    0bBCDEF
    B - Menu click bit
    C - ACB state change bit
    D - Number change bit
    E - Menu selected entry change bit
    F - Menu change bit
*/
typedef volatile __UINT8_TYPE__ state_t;
#define BUTTON_CLICK_BIT 16
#define ACB_UPDATE_BIT 8
#define NUM_CHANGE_BIT 4
#define MENU_CHANGE_BIT 2
#define MENU_ACTIVE_BIT 1
void log_state(state_t* state);

// Структура для хранения данных карты
#define MIFARE_BLOCK_SIZE 16
struct mifare_card_data {
    __UINT16_TYPE__ block_count;
    __UINT8_TYPE__* block_data;
};
typedef struct mifare_card_data mifare_card_data_t;
typedef struct mifare_card_data *mifare_card_data_handle_t;