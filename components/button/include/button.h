#ifndef BUTTON_H
#define BUTTON_H

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    BUTTON_EVENT_NONE = 0,  // 无按键事件
    BUTTON_EVENT_SHORT_PRESS,   // 短按事件
    BUTTON_EVENT_LONG_PRESS     // 长按事件
} button_event_t;

typedef enum {
    BUTTON_ID_K1 = 0,
    BUTTON_ID_K2,
    BUTTON_ID_K3,
    BUTTON_ID_K4,
    BUTTON_ID_MAX
} button_id_t;

esp_err_t button_init(void);
button_event_t button_scan(void);
button_event_t button_scan_key(button_id_t button);
bool button_is_pressed(void);
bool button_is_key_pressed(button_id_t button);

#endif
