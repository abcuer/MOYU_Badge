#ifndef __BSP_IIC_H
#define __BSP_IIC_H

#include "driver/gpio.h"
#include "esp_err.h"

// 成功与失败的定义，兼容你原有的逻辑
#define SUCCESS 0
#define ERROR   1

typedef struct
{
    gpio_num_t sda_io;  // SDA 引脚编号
    gpio_num_t scl_io;  // SCL 引脚编号
} iic_bus_t;

void SDA_Output(iic_bus_t *bus, uint8_t val);
void SCL_Output(iic_bus_t *bus, uint8_t val);

// 基础 IIC 操作
void IICInit(iic_bus_t *bus);
void IICStart(iic_bus_t *bus);
void IICStop(iic_bus_t *bus);
uint8_t IICWaitAck(iic_bus_t *bus);
void IICSendAck(iic_bus_t *bus);
void IICSendNotAck(iic_bus_t *bus);
void IICSendByte(iic_bus_t *bus, uint8_t cSendByte);
uint8_t IICReceiveByte(iic_bus_t *bus);

// 业务读写接口
uint8_t IIC_Write_One_Byte(iic_bus_t *bus, uint8_t daddr, uint8_t reg, uint8_t data);
uint8_t IIC_Write_Multi_Byte(iic_bus_t *bus, uint8_t daddr, uint8_t reg, uint8_t length, uint8_t buff[]);
uint8_t IIC_Read_One_Byte(iic_bus_t *bus, uint8_t daddr, uint8_t reg);
uint8_t IIC_Read_Multi_Byte(iic_bus_t *bus, uint8_t daddr, uint8_t reg, uint8_t length, uint8_t buff[]);

#endif