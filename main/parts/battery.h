//
// Created by s1pepega on 21.11.2026.
//

#ifndef CWS5Y26_BATTERY_H
#define CWS5Y26_BATTERY_H
#include "esp_adc/adc_oneshot.h"
#include "core.h"

void init_battery_oneshot(adc_oneshot_unit_handle_t* adc1_handle, adc_unit_t unit, adc_atten_t atten, adc_bitwidth_t bitwidth, adc_channel_t adc_channel);
bool recalc_acb_seg(int adc_raw, int * prev_after_eval, int * acb_seg_count);
void one_shot_with_recalc(adc_channel_t adc_channel, adc_oneshot_unit_handle_t adc1_handle, int* adc_raw, int * prev_after_eval, int * acb_seg_count, state_t* state);

#endif //CWS5Y26_BATTERY_H
