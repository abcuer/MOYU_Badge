#ifndef __KEY_H
#define __KEY_H

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KEY_NUM 1
#define USER_KEY_PIN 19

#define KEY_DEBOUNCE_MS 20
#define KEY_LONG_PRESS_MS 1000
#define KEY_SUPER_LONG_PRESS_MS 2000

typedef enum {
    KEY_USER = 0,
} key_type_t;

typedef enum {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_SHORT,
    KEY_EVENT_LONG,
    KEY_EVENT_SUPER_LONG,
} key_event_e;

typedef enum {
    KEY_IDLE = 0,
    KEY_CONFIRM,
    KEY_PRESSING,
} key_fsm_e;

typedef struct {
    gpio_num_t gpio_pin;
    uint8_t press_level;
} key_static_param_s;

typedef struct {
    key_fsm_e state;
    uint32_t last_tick;
    bool long_triggered;
    bool super_long_triggered;
} key_running_param_s;

typedef struct {
    key_static_param_s static_param;
    key_running_param_s running_param;
} key_instance_s;

void key_device_init(void);
bool key_is_idle(key_type_t type);
key_event_e key_get_event(key_type_t type);
void key_reset_fsm(key_type_t type);

#ifdef __cplusplus
}
#endif

#endif
