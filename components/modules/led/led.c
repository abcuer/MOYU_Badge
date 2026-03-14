#include "led.h"

// 记录 LED 的当前电平状态，用于实现 TOGGLE
static uint8_t led_states[LED_NUM] = {0};

// 获取对应类型的引脚号
static gpio_num_t get_gpio_pin(led_type_t type) {
    return (type == LED_RED) ? rLED_PIN : bLED_PIN;
}

/**
 * @brief 初始化 LED GPIO
 */
void led_init(void)
{
    // 使用位掩码一次性配置多个引脚
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,      // 禁用中断
        .mode = GPIO_MODE_INPUT_OUTPUT,      // 设置为输入输出模式（TOGGLE 需要读取当前状态或维护状态）
        .pin_bit_mask = (1ULL << rLED_PIN) | (1ULL << bLED_PIN), // 同时配置红蓝灯
        .pull_down_en = 0,                   // 禁用下拉
        .pull_up_en = 0                      // 禁用上拉
    };
    gpio_config(&io_conf);

    // 默认初始状态为关闭
    led_set_mode(LED_RED, LED_OFF);
    led_set_mode(LED_BLUE, LED_OFF);
}

/**
 * @brief 执行 LED 操作
 * @param type LED 类型
 * @param mode 操作模式 (LED_ON, LED_OFF, LED_TOGGLE)
 */
void led_set_mode(led_type_t type, led_mode_t mode)
{
    if (type >= LED_NUM) return;

    gpio_num_t pin = get_gpio_pin(type);
    uint32_t target_level = 0;

    switch (mode) {
        case LED_OFF:
            target_level = 0;
            break;
        case LED_ON:
            target_level = 1;
            break;
        case LED_TOGGLE:
            // 翻转当前记录的状态
            target_level = !led_states[type];
            break;
    }

    // 更新硬件电平
    gpio_set_level(pin, target_level);
    // 更新内部记录的状态
    led_states[type] = target_level;
}