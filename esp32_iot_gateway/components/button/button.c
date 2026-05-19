#include "button.h"

esp_err_t button_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

button_event_t button_scan(void)
{
    return BUTTON_EVENT_NONE;
}

bool button_is_pressed(void)
{
    return false;
}
