#include "bsp_iic.h"
#include "bsp_delay.h"

/**
  * @brief SDA线模式切换 (在 ESP32 上，如果配置为 Open-Drain，其实可以不用频繁切换方向)
  */
void SDA_Input_Mode(iic_bus_t *bus)
{
    gpio_set_direction(bus->sda_io, GPIO_MODE_INPUT);
}

void SDA_Output_Mode(iic_bus_t *bus)
{
    // 推荐使用 GPIO_MODE_INPUT_OUTPUT_OD (输入输出开漏)，这样读取时不需要切换方向
    gpio_set_direction(bus->sda_io, GPIO_MODE_INPUT_OUTPUT_OD);
}

/**
  * @brief 基础电平控制
  */
void SDA_Output(iic_bus_t *bus, uint8_t val)
{
    gpio_set_level(bus->sda_io, val);
}

void SCL_Output(iic_bus_t *bus, uint8_t val)
{
    gpio_set_level(bus->scl_io, val);
}

static inline uint8_t SDA_Input(iic_bus_t *bus)
{
    return gpio_get_level(bus->sda_io);
}

/**
  * @brief IIC起始信号
  */
void IICStart(iic_bus_t *bus)
{
    SDA_Output(bus, 1);
    SCL_Output(bus, 1);
    delay_us(2);
    SDA_Output(bus, 0);
    delay_us(2);
    SCL_Output(bus, 0);
    delay_us(2);
}

/**
  * @brief IIC结束信号
  */
void IICStop(iic_bus_t *bus)
{
    SCL_Output(bus, 0);
    SDA_Output(bus, 0);
    delay_us(2);
    SCL_Output(bus, 1);
    delay_us(2);
    SDA_Output(bus, 1);
    delay_us(2);
}

/**
  * @brief IIC等待确认信号
  */
uint8_t IICWaitAck(iic_bus_t *bus)
{
    uint8_t cErrTime = 5;
    SDA_Output(bus, 1); // 释放总线等待从机拉低
    delay_us(2);
    SCL_Output(bus, 1);
    delay_us(2);
    
    while(SDA_Input(bus))
    {
        cErrTime--;
        delay_us(2);
        if (0 == cErrTime)
        {
            IICStop(bus);
            return ERROR;
        }
    }
    SCL_Output(bus, 0);
    delay_us(2);
    return SUCCESS;
}

void IICSendAck(iic_bus_t *bus)
{
    SDA_Output(bus, 0);
    delay_us(2);
    SCL_Output(bus, 1);
    delay_us(2);
    SCL_Output(bus, 0);
    delay_us(2);
}

void IICSendNotAck(iic_bus_t *bus)
{
    SDA_Output(bus, 1);
    delay_us(2);
    SCL_Output(bus, 1);
    delay_us(2);
    SCL_Output(bus, 0);
    delay_us(2);
}

void IICSendByte(iic_bus_t *bus, uint8_t cSendByte)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        SDA_Output(bus, (cSendByte & 0x80) >> 7);
        delay_us(2);
        SCL_Output(bus, 1);
        delay_us(2);
        SCL_Output(bus, 0);
        cSendByte <<= 1;
    }
}

uint8_t IICReceiveByte(iic_bus_t *bus)
{
    uint8_t cR_Byte = 0;
    SDA_Output(bus, 1); // 释放总线
    for (uint8_t i = 0; i < 8; i++)
    {
        cR_Byte <<= 1;
        SCL_Output(bus, 1);
        delay_us(2);
        if(SDA_Input(bus)) cR_Byte |= 0x01;
        SCL_Output(bus, 0);
        delay_us(2);
    }
    return cR_Byte;
}

// --- 业务封装接口 ---

uint8_t IIC_Write_One_Byte(iic_bus_t *bus, uint8_t daddr, uint8_t reg, uint8_t data)
{                                                                          
    IICStart(bus);  
    IICSendByte(bus, daddr << 1);     
    if(IICWaitAck(bus)) { IICStop(bus); return 1; }
    IICSendByte(bus, reg);
    IICWaitAck(bus);                                                 
    IICSendByte(bus, data);                            
    IICWaitAck(bus);             
    IICStop(bus);
    return 0;
}

uint8_t IIC_Read_One_Byte(iic_bus_t *bus, uint8_t daddr, uint8_t reg)
{
    uint8_t data = 0;

    IICStart(bus);
    IICSendByte(bus, daddr << 1);
    if (IICWaitAck(bus)) {
        IICStop(bus);
        return 0xFF;
    }
    IICSendByte(bus, reg);
    IICWaitAck(bus);

    IICStart(bus);
    IICSendByte(bus, (daddr << 1) + 1);
    IICWaitAck(bus);
    data = IICReceiveByte(bus);
    IICSendNotAck(bus);
    IICStop(bus);

    return data;
}

uint8_t IIC_Read_Multi_Byte(iic_bus_t *bus, uint8_t daddr, uint8_t reg, uint8_t length, uint8_t buff[])
{
    IICStart(bus);
    IICSendByte(bus, daddr << 1);
    if(IICWaitAck(bus)) { IICStop(bus); return 1; }
    IICSendByte(bus, reg);
    IICWaitAck(bus);
    
    IICStart(bus);
    IICSendByte(bus, (daddr << 1) + 1);
    IICWaitAck(bus);
    for(uint8_t i = 0; i < length; i++)
    {
        buff[i] = IICReceiveByte(bus);
        if(i < length - 1) IICSendAck(bus);
    }
    IICSendNotAck(bus);
    IICStop(bus);
    return 0;
}

/**
  * @brief 初始化 GPIO
  */
void IICInit(iic_bus_t *bus)
{
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD, // 开漏模式，必须接上拉电阻
        .pin_bit_mask = (1ULL << bus->sda_io) | (1ULL << bus->scl_io),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE, // 开启内部上拉（建议外部再接4.7k电阻）
    };
    gpio_config(&io_conf);

    // 默认输出高电平（空闲状态）
    gpio_set_level(bus->sda_io, 1);
    gpio_set_level(bus->scl_io, 1);
}
