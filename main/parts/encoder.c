//
// Created by s1pepega on 21.11.2026.
//
#include "../encoder.h"


void init_encoder(pcnt_unit_handle_t * pcnt_unit, int min, int max, uint32_t glitch, int clk_gpio, int dt_gpio)
{
    pcnt_unit_config_t unit_config = {
        .high_limit = max,
        .low_limit = min,
        .flags.accum_count = true,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, pcnt_unit));
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = glitch,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(*pcnt_unit, &filter_config));
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = clk_gpio,
        .level_gpio_num = dt_gpio,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(*pcnt_unit, &chan_a_config, &pcnt_chan_a));
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = dt_gpio,
        .level_gpio_num = clk_gpio,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(*pcnt_unit, &chan_b_config, &pcnt_chan_b));
    ESP_ERROR_CHECK(
        pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE
        ));
    ESP_ERROR_CHECK(
        pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    ESP_ERROR_CHECK(
        pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE
        ));
    ESP_ERROR_CHECK(
        pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_unit_enable(*pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(*pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(*pcnt_unit));
}
void encoder_read(pcnt_unit_handle_t * pcnt_unit, int * pulse_count, int * prev_pulse_count, state_t * state, int * selected_menu, int * selected_num)
{
    ESP_ERROR_CHECK(pcnt_unit_get_count(*pcnt_unit, pulse_count));
    int diff = (*pulse_count - *prev_pulse_count) / 2;
    if (diff != 0)
    {
        if (*state & MENU_ACTIVE_BIT)
        {
            if (*pulse_count == 0 && diff / 20 > 1)
                diff = diff < 0 ? -1 : 1;
            *selected_menu += diff;
            if (*selected_menu > 3)
                *selected_menu = 0;
            else if (*selected_menu < 0)
                *selected_menu = 3;
            *state |= MENU_CHANGE_BIT;
        }
        else
        {
            if (*pulse_count == 0 && diff / 20 > 1)
                diff = diff < 0 ? -1 : 1;
            *selected_num += diff;
            if (*selected_num > 20)
                *selected_num = 0;
            else if (*selected_num < 0)
                *selected_num = 20;
            *state |= NUM_CHANGE_BIT;
        }
        *prev_pulse_count = *pulse_count;
    }
}


void init_encoder_button(int sw_gpio, gpio_isr_t gpio_isr_handler)
{
    esp_rom_gpio_pad_select_gpio(sw_gpio);
    gpio_set_direction(sw_gpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(sw_gpio, GPIO_FLOATING);
    gpio_set_intr_type(sw_gpio, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    gpio_isr_handler_add(sw_gpio, gpio_isr_handler, NULL);
}
