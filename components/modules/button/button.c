#include "button.h"
#include "string.h"
#include "stdlib.h"
#include "esp_timer.h"
#include "esp_log.h"

#define TAG "BUTTON"

static button_info_t *button_head = NULL; 
static esp_timer_handle_t button_timer_handle;
static bool timer_running = false;

// 定时器回调函数：轮询链表中的所有按键
static void button_handle(void *arg);

esp_err_t button_event_set(button_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    button_info_t *btn = (button_info_t *)malloc(sizeof(button_info_t));
    if (!btn) return ESP_ERR_NO_MEM;
    
    memset(btn, 0, sizeof(button_info_t));
    memcpy(&btn->btn_cfg, cfg, sizeof(button_config_t));
    btn->next = NULL; // 显式置空

    // 链表插入：尾插法
    if(!button_head) {
        button_head = btn;
    } else {
        button_info_t *info = button_head;
        while(info->next) info = info->next;
        info->next = btn;
    }

    // 只在第一次调用时初始化硬件定时器
    if(!timer_running) {
        const esp_timer_create_args_t button_timer_args = {
            .callback = button_handle,
            .arg = NULL, // 回调里直接用全局变量 button_head
            .name = "button_timer"
        };
        esp_err_t err = esp_timer_create(&button_timer_args, &button_timer_handle);
        if (err != ESP_OK) return err;

        // 启动周期定时器，单位是微秒 (us)
        esp_timer_start_periodic(button_timer_handle, btn_interval * 1000); 
        timer_running = true;
    }
    
    return ESP_OK;
}

static void button_handle(void *arg)
{
    button_info_t *btn_info = button_head;
    int interval = (int)arg;
    for(; btn_info; btn_info = btn_info->next) 
    {
        switch(btn_info->state)
        {
            case BUTTON_RELEASE:
                if(btn_info->btn_cfg.get_level_cb(btn_info->btn_cfg.gpio_num) == btn_info->btn_cfg.active_level) 
                {
                    btn_info->state = BUTTON_PRESS;
                    btn_info->press_cnt += interval;
                }
                break;

            case BUTTON_PRESS:
                if(btn_info->btn_cfg.get_level_cb(btn_info->btn_cfg.gpio_num) == btn_info->btn_cfg.active_level) 
                {
                    btn_info->press_cnt += interval;
                    if(btn_info->press_cnt >= 20)
                    {
                        if(btn_info->btn_cfg.short_press_cb)
                            btn_info->btn_cfg.short_press_cb(btn_info->btn_cfg.gpio_num);
                        btn_info->state = BUTTON_HOLD;
                    }
                }
                else 
                {
                    btn_info->state = BUTTON_RELEASE;
                    btn_info->press_cnt = 0;
                }
                break;

            case BUTTON_HOLD:
                if(btn_info->btn_cfg.get_level_cb(btn_info->btn_cfg.gpio_num) == btn_info->btn_cfg.active_level) 
                {
                    btn_info->press_cnt += interval;
                    if(btn_info->press_cnt >= btn_info->btn_cfg.long_press_time)
                    {
                        if(btn_info->btn_cfg.long_press_cb)
                            btn_info->btn_cfg.long_press_cb(btn_info->btn_cfg.gpio_num);
                        btn_info->state = BUTTON_LONG_PRESS_HOLD;
                    }
                }
                else 
                {
                    btn_info->state = BUTTON_RELEASE;
                    btn_info->press_cnt = 0;
                }
                break;

            case BUTTON_LONG_PRESS_HOLD:
                if(btn_info->btn_cfg.get_level_cb(btn_info->btn_cfg.gpio_num) != btn_info->btn_cfg.active_level) {
                    btn_info->state = BUTTON_RELEASE;
                    btn_info->press_cnt = 0;
                }
                break;
            default:
                break;
        }
    }
}
