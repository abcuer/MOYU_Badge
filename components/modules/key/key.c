#include "key.h"

static key_instance_s keys[KEY_NUM];

SemaphoreHandle_t key_sem = NULL;

void key_reset_fsm(key_type_t type)
{
    if (type >= KEY_NUM) {
        return;
    }

    key_instance_s *ins = &keys[type];
    ins->running_param.state = KEY_IDLE;
    ins->running_param.long_triggered = false;
    ins->running_param.super_long_triggered = false;
    ins->running_param.last_tick = 0;
}

static uint8_t get_key_level(key_type_t type)
{
    return gpio_get_level(keys[type].static_param.gpio_pin);
}

bool key_is_idle(key_type_t type)
{
    if (type >= KEY_NUM) {
        return true;
    }

    return (keys[type].running_param.state == KEY_IDLE);
}

void key_device_init(void)
{
    keys[KEY_USER].static_param.gpio_pin = USER_KEY_PIN;
    keys[KEY_USER].static_param.press_level = 0;
    keys[KEY_USER].running_param.state = KEY_IDLE;
    keys[KEY_USER].running_param.long_triggered = false;
    keys[KEY_USER].running_param.super_long_triggered = false;
    keys[KEY_USER].running_param.last_tick = 0;

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << USER_KEY_PIN),
        .pull_up_en = 1,
        .pull_down_en = 0,
    };
    gpio_config(&io_conf);
}

key_event_e key_get_event(key_type_t type)
{
    if (type >= KEY_NUM) {
        return KEY_EVENT_NONE;
    }

    key_instance_s *ins = &keys[type];
    uint8_t curr_level = get_key_level(type);
    bool is_pressed = (curr_level == ins->static_param.press_level);
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    key_event_e event = KEY_EVENT_NONE;

    switch (ins->running_param.state) {
        case KEY_IDLE:
            if (is_pressed) {
                ins->running_param.last_tick = now;
                ins->running_param.state = KEY_CONFIRM;
            }
            break;

        case KEY_CONFIRM:
            if (is_pressed) {
                if ((now - ins->running_param.last_tick) >= KEY_DEBOUNCE_MS) {
                    ins->running_param.state = KEY_PRESSING;
                    ins->running_param.long_triggered = false;
                    ins->running_param.super_long_triggered = false;
                }
            } else {
                ins->running_param.state = KEY_IDLE;
            }
            break;

        case KEY_PRESSING:
            if (is_pressed) {
                uint32_t press_ms = now - ins->running_param.last_tick;
                if (!ins->running_param.long_triggered && press_ms >= KEY_LONG_PRESS_MS) {
                    ins->running_param.long_triggered = true;
                    event = KEY_EVENT_LONG;
                } else if (!ins->running_param.super_long_triggered &&
                           press_ms >= KEY_SUPER_LONG_PRESS_MS) {
                    ins->running_param.super_long_triggered = true;
                    event = KEY_EVENT_SUPER_LONG;
                }
            } else {
                if (!ins->running_param.long_triggered && !ins->running_param.super_long_triggered) {
                    event = KEY_EVENT_SHORT;
                }
                ins->running_param.state = KEY_IDLE;
            }
            break;
    }

    return event;
}
