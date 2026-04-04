#include "max30102.h"
#include "bsp_iic.h"
#include "bsp_delay.h"

iic_bus_t max30102_bus = {
    .sda_io = MAX_SDA_PIN, 
    .scl_io = MAX_SCL_PIN
};

uint32_t fifo_red; // 改为 uint32_t 以支持 18-bit 精度
uint32_t fifo_ir;

static void max30102_write_reg(uint8_t reg, uint8_t data) {
    IIC_Write_One_Byte(&max30102_bus, MAX30102_ADDRESS, reg, data);
}

void max30102_init(void) 
{
    IICInit(&max30102_bus);   
    delay_ms(10); 
    
    max30102_write_reg(REG_MODE_CONFIG, 0x40);   // 复位
    delay_ms(50);  

    // 100Hz 采样率下，平均2次，等于每 20ms 吐出一组数据
    max30102_write_reg(REG_FIFO_CONFIG, 0x3F);   
    
    max30102_write_reg(REG_MODE_CONFIG, 0x03);   // SpO2模式 (Red + IR)
    max30102_write_reg(REG_SPO2_CONFIG, 0x27);   // 100Hz, 16bit精度
    max30102_write_reg(REG_LED1_PA, 0x24);       // LED电流 7.2mA
    max30102_write_reg(REG_LED2_PA, 0x24);       
    max30102_write_reg(REG_PILOT_PA, 0x7F);      
}
/**
 * @brief 从 FIFO 读取一组原始数据
 */
void max30102_read_fifo(void) 
{
    uint8_t data[6];
    // 使用你 bsp_iic 中的多字节读取接口
    IIC_Read_Multi_Byte(&max30102_bus, MAX30102_ADDRESS, REG_FIFO_DATA, 6, data);
    
    // 组合数据（24-bit 原始数据）
    fifo_red = ((uint32_t)data[0] << 16 | (uint32_t)data[1] << 8 | data[2]) & 0x03FFFF;
    fifo_ir  = ((uint32_t)data[3] << 16 | (uint32_t)data[4] << 8 | data[5]) & 0x03FFFF;
}

void max30102_sleep(bool enable) 
{
    if (enable) {
        max30102_write_reg(REG_MODE_CONFIG, 0x40 | 0x80); // 保持原复位标志，或上 0x80 进入休眠
    } else {
        max30102_write_reg(REG_MODE_CONFIG, 0x03); // 恢复 SpO2 模式模式唤醒
    }
}