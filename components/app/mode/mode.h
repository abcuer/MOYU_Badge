#ifndef __MODE_H
#define __MODE_H

#include <stdint.h>
#include <stdbool.h>

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
void enter_light_sleep(void);

extern bool in_select;
extern StepData_t step_data;
extern uint32_t last_action_time; 

#endif