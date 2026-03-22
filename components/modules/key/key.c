#include "key.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static key_instance_s keys[KEY_NUM];

static uint8_t get_key_level(key_type_t type) {
    return gpio_get_level(keys[type].static_param.gpio_pin);
}

void key_device_init(void) {
    keys[KEY_USER].static_param.gpio_pin = USER_KEY_PIN;
    keys[KEY_USER].static_param.press_level = 0; // 假设0为按下
    keys[KEY_USER].running_param.state = KEY_IDLE;
    keys[KEY_USER].running_param.long_triggered = false;

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << USER_KEY_PIN),
        .pull_up_en = 1, // 默认上拉
    };
    gpio_config(&io_conf);
}

/**
 * @brief 按键状态机处理
 * @return key_event_e 返回按键事件
 */
key_event_e key_get_event(key_type_t type) 
{
    if (type >= KEY_NUM) return KEY_EVENT_NONE;

    key_instance_s *ins = &keys[type];
    uint8_t curr_level = get_key_level(type);
    bool is_pressed = (curr_level == ins->static_param.press_level);
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS; // 转换为毫秒
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
                }
            } else {
                ins->running_param.state = KEY_IDLE;
            }
            break;

        case KEY_PRESSING:
            if (is_pressed) {
                // 检测长按
                if (!ins->running_param.long_triggered && 
                    (now - ins->running_param.last_tick) >= KEY_LONG_PRESS_MS) {
                    ins->running_param.long_triggered = true;
                    event = KEY_EVENT_LONG; // 达到时间，立即返回长按事件
                }
            } else {
                // 释放按键
                if (!ins->running_param.long_triggered) {
                    event = KEY_EVENT_SHORT; // 如果没达到长按时间就释放，判定为短按
                }
                ins->running_param.state = KEY_IDLE;
            }
            break;
    }

    return event;
}