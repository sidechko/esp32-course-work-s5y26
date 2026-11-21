//
// Created by s1pepega on 21.11.2026.
//

#ifndef CWS5Y26_STATE_INFO_H
#define CWS5Y26_STATE_INFO_H

#endif //CWS5Y26_STATE_INFO_H
/*
    0bBCDEF
    B - Menu click bit
    C - ACB state change bit
    D - Number change bit
    E - Menu selected entry change bit
    F - Menu change bit
*/
typedef volatile uint8_t state_t;
#define BUTTON_CLICK_BIT 16
#define ACB_UPDATE_BIT 8
#define NUM_CHANGE_BIT 4
#define MENU_CHANGE_BIT 2
#define MENU_ACTIVE_BIT 1
void log_sate(state_t* state);

