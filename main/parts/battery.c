//
// Created by s1pepega on 21.11.2026.
//
#include "battery.h"


void init_battery_oneshot(adc_oneshot_unit_handle_t* adc1_handle, adc_unit_t unit, adc_atten_t atten, adc_bitwidth_t bitwidth, adc_channel_t adc_channel)
{
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = unit,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, adc1_handle));
    adc_oneshot_chan_cfg_t config = {
        .atten = atten,
        .bitwidth = bitwidth,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(*adc1_handle, adc_channel, &config));
}

bool recalc_acb_seg(int adc_raw, int * prev_after_eval, int * acb_seg_count){
    if(adc_raw < 1302)
        adc_raw = 1302;
    else if(adc_raw > 2562)
        adc_raw = 2562;
    if(*prev_after_eval << 5 == adc_raw << 5)
        return false;
    *prev_after_eval = adc_raw;
    const int count = 29 - (2562 - adc_raw) / 42;
    if(*acb_seg_count != count){
        *acb_seg_count = count;
        return true;
    }
    return false;
}

void one_shot_with_recalc(
    adc_channel_t adc_channel,
    adc_oneshot_unit_handle_t adc1_handle,
    int* adc_raw,
    int * prev_after_eval,
    int * acb_seg_count,
    state_t * state
    )
{
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, adc_channel, adc_raw));
    if (recalc_acb_seg(*adc_raw, prev_after_eval, acb_seg_count))
        *state |= ACB_UPDATE_BIT;
}
