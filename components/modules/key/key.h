#ifndef __KEY_H
#define __KEY_H

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KEY_NUM 1 
#define USER_KEY_PIN    3 

// 触发阈值定义（单位：ms）
#define KEY_DEBOUNCE_MS    20    // 消抖时间
#define KEY_LONG_PRESS_MS  1000  // 超过1.5秒判定为长按

typedef enum {
    KEY_USER = 0,
} key_type_t;

typedef enum {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_SHORT,    // 单击（弹起时触发）
    KEY_EVENT_LONG,     // 长按（达到时间立即触发或弹起触发，本示例采用达到时间触发）
} key_event_e;

typedef enum {
    KEY_IDLE = 0,       // 空闲
    KEY_CONFIRM,        // 确认消抖
    KEY_PRESSING,       // 正在按下
} key_fsm_e;

typedef struct {
    gpio_num_t gpio_pin;
    uint8_t press_level;
} key_static_param_s;

typedef struct {
    key_fsm_e state;      // 状态机当前状态
    uint32_t last_tick;   // 记录进入状态的时间点
    bool long_triggered;  // 长按是否已触发标志位，防止重复触发
} key_running_param_s;

typedef struct {
    key_static_param_s  static_param;
    key_running_param_s running_param;
} key_instance_s;

void key_device_init(void);
bool key_is_idle(key_type_t type);
key_event_e key_get_event(key_type_t type);
void key_reset_fsm(key_type_t type);

#ifdef __cplusplus
}
#endif

extern SemaphoreHandle_t key_sem;

#endif