#ifndef __BLOODS_H
#define __BLOODS_H

#include <stdbool.h>
#include <stdint.h>

#define SAMPLE_RATE_MS   10U    // 采样间隔10ms = 100Hz
#define SMOOTH_SIZE      10U    // 滑动平均窗口
#define PEAK_MIN_DIST    40U    // 峰间最小距离(点数)，对应400ms = 150bpm上限
#define PEAK_MIN_HEIGHT  500U   // 峰谷最小幅度，过滤噪声
#define HR_BUF_SIZE      5U
#define DC_ALPHA         0.98f

typedef struct {
    int heart;      // 心率 bpm
    float SpO2;     // 血氧 %
    bool valid;     // 数据是否有效
} BloodData_t;

// 血氧任务状态
typedef enum {
    BLOOD_IDLE = 0,   // 等待手指放上
    BLOOD_SAMPLING,   // 采集中
    BLOOD_DONE,       // 结果就绪
} BloodTaskState_t;

extern BloodData_t b_data;
extern BloodTaskState_t b_state;

// 峰谷法核心函数
void blood_sample_once(void);      // 采一个点并实时处理
void blood_reset(void);            // 重置状态
void blood_detect(void);

#endif
