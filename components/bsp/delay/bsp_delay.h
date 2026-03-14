#ifndef _BSP_DELAY_H
#define _BSP_DELAY_H

#include "stdint.h"


// 1. 微秒级延时：用于模拟时序或极短的硬件等待
void delay_us(uint32_t us);

// 2. 毫秒级延时：用于初始化阶段的唤醒等待
void delay_ms(uint32_t ms);

#endif