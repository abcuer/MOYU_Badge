#ifndef __MODE_H
#define __MODE_H

#include <stdint.h>
#include <stdbool.h>

// 阈值参数（可根据实测调整）
#define STEP_THRESHOLD_HIGH  1.2f   // 高阈值 g
#define STEP_THRESHOLD_LOW   0.85f  // 低阈值 g
#define STEP_MIN_INTERVAL_MS 250    // 两步最小间隔，防抖（对应240步/分钟上限）
#define STEP_MAX_INTERVAL_MS 2000   // 两步最大间隔，超过认为停止走路

typedef struct {
    uint32_t total_steps;    // 总步数
    uint32_t today_steps;    // 今日步数
    float    cadence;        // 步频 步/分钟
    bool     is_walking;     // 是否正在走路
} StepData_t;

// 状态机
typedef enum {
    STEP_STATE_IDLE = 0,  // 等待SVM超过高阈值
    STEP_STATE_HIGH,      // 已超过高阈值，等待下降到低阈值
} StepFSM_t;

void key_scan(void);
void step_detect(void);
void enter_light_sleep(void);

extern bool in_select;
extern StepData_t step_data;
extern uint32_t last_action_time; 

#endif