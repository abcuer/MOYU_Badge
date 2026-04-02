#include "key.h"

static key_instance_s keys[KEY_NUM];

// 🆕 1. 定义按键信号量
SemaphoreHandle_t key_sem = NULL;


/**
 * @brief 强制复位指定按键的状态机参数
 * @param type 按键索引
 */
void key_reset_fsm(key_type_t type)
{
    if (type >= KEY_NUM) return; // 越界安全检查

    key_instance_s *ins = &keys[type]; //

    // 🎯 强制拉回 IDLE 空闲状态
    ins->running_param.state = KEY_IDLE; 
    ins->running_param.long_triggered = false;
    ins->running_param.last_tick = 0; 
}


static uint8_t get_key_level(key_type_t type) {
    return gpio_get_level(keys[type].static_param.gpio_pin);
}

// 🆕 2. 中断服务函数 (ISR)，运行在 RAM 中
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    if (key_sem != NULL) {
        // 解锁阻塞的按键任务
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(key_sem, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR(); // 强制上下文切换，让按键任务立刻执行
        }
    }
}

/**
 * @brief 判断按键状态机是否回归空闲
 */
bool key_is_idle(key_type_t type) 
{
    if (type >= KEY_NUM) return true;
    return (keys[type].running_param.state == KEY_IDLE);
}

void key_device_init(void) {
    // 🆕 创建二值信号量
    key_sem = xSemaphoreCreateBinary();

    keys[KEY_USER].static_param.gpio_pin = USER_KEY_PIN;
    keys[KEY_USER].static_param.press_level = 0; 
    keys[KEY_USER].running_param.state = KEY_IDLE;
    keys[KEY_USER].running_param.long_triggered = false;

    // 🆕 配置 GPIO 为任何电平跳变触发中断 (按下和释放都捕捉)
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE, // 开启双边沿中断
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << USER_KEY_PIN),
        .pull_up_en = 1, 
    };
    gpio_config(&io_conf);

    // 🆕 注册全局 GPIO ISR 服务
    gpio_install_isr_service(0);
    // 🆕 绑定中断回调函数
    gpio_isr_handler_add(USER_KEY_PIN, gpio_isr_handler, (void*) USER_KEY_PIN);
}

/**
 * @brief 按键状态机处理
 * @return key_event_e 返回按键事件
 */
key_event_e key_get_event(key_type_t type) 
{
    if (type > KEY_NUM) return KEY_EVENT_NONE;

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