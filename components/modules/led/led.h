#ifndef __LED_H
#define __LED_H

#include "driver/gpio.h"

#define LED_NUM 2 // LED 类型数量

// 1. 指定 LED 引脚宏定义
#define rLED_PIN 6
#define bLED_PIN 7

// 2. 定义 LED 类型枚举
typedef enum {
    LED_RED = 0,
    LED_BLUE,
} led_type_t;

// 3. 定义 LED 操作模式
typedef enum {
    LED_OFF = 0,
    LED_ON,
    LED_TOGGLE
} led_mode_t;

// 函数声明
void led_init(void);
void led_set_mode(led_type_t type, led_mode_t mode);

#endif /* __LED_H */