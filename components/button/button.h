#ifndef __BUTTON_H
#define __BUTTON_H

#include "esp_err.h"

#define btn_interval 5 // 按键扫描间隔

// 按键回调函数
typedef void(*button_press_cb_t)(int gpio_num);
// 获取gpio电平的操作回调函数
typedef int (*button_getlevel_cb_t)(int gpio_num);

typedef struct 
{
    int gpio_num;       // 按键连接的 GPIO 引脚
    int active_level;   // 按键的有效电平 (0 或 1)
    int long_press_time; // 长按时间阈值 (单位: ms)
    button_press_cb_t short_press_cb; // 短按回调函数
    button_press_cb_t long_press_cb;  // 长按回调函数
    button_getlevel_cb_t get_level_cb; // 获取GPIO电平的回调函数
}button_config_t;

typedef enum 
{
    BUTTON_RELEASE = 0,         // 按键松开
    BUTTON_PRESS,               // 消抖状态(按键按下)
    BUTTON_HOLD,                // 按住状态
    BUTTON_LONG_PRESS_HOLD      // 等待松手
}BUTTONM_STATE;

typedef struct Button_info
{
    button_config_t btn_cfg;     // 按键配置
    BUTTONM_STATE   state;       // 当前按键状态
    int press_cnt;               // 按压计数
    struct Button_info *next;    // 下一个按键参数
}button_info_t; 
    

#endif /* __BUTTON_H */