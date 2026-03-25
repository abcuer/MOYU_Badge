#ifndef __MAX30102_H
#define __MAX30102_H

#include <stdint.h>
#include "bsp_iic.h"

#define MAX_SDA_PIN 14
#define MAX_SCL_PIN 13

/* 器件地址 */
#define MAX30102_ADDRESS    0x57

/* 寄存器地址映射 */
#define REG_INTR_STATUS_1   0x00
#define REG_INTR_STATUS_2   0x01
#define REG_INTR_ENABLE_1   0x02
#define REG_INTR_ENABLE_2   0x03
#define REG_FIFO_WR_PTR     0x04
#define REG_OVF_COUNTER     0x05
#define REG_FIFO_RD_PTR     0x06
#define REG_FIFO_DATA       0x07
#define REG_FIFO_CONFIG     0x08
#define REG_MODE_CONFIG     0x09
#define REG_SPO2_CONFIG     0x0A
#define REG_LED1_PA         0x0C  // Red LED
#define REG_LED2_PA         0x0D  // IR LED
#define REG_PILOT_PA        0x10
#define REG_MULTI_LED_CTRL1 0x11
#define REG_MULTI_LED_CTRL2 0x12
#define REG_TEMP_INTR       0x1F
#define REG_TEMP_FRAC       0x20
#define REG_TEMP_CONFIG     0x21
#define REG_REV_ID          0xFE
#define REG_PART_ID         0xFF

/* 外部变量声明 */
extern uint32_t fifo_red; // 修改为 uint32_t 防止 18位数据溢出
extern uint32_t fifo_ir;
extern iic_bus_t max30102_bus;

/* 函数原型声明 */
void max30102_init(void);
void max30102_read_fifo(void); // 内部封装 IIC 读取 6 字节逻辑
void max30102_sleep(bool enable);

#endif