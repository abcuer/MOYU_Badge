#include "max30102.h"
#include "bsp_iic.h"
#include "bsp_delay.h"

iic_bus_t max30102_bus = {
    .sda_io = MAX_SDA_PIN,
    .scl_io = MAX_SCL_PIN
};

uint32_t fifo_red;
uint32_t fifo_ir;

static void max30102_write_reg(uint8_t reg, uint8_t data)
{
    IIC_Write_One_Byte(&max30102_bus, MAX30102_ADDRESS, reg, data);
}

static uint8_t max30102_read_reg(uint8_t reg)
{
    return IIC_Read_One_Byte(&max30102_bus, MAX30102_ADDRESS, reg);
}

static void max30102_clear_fifo(void)
{
    max30102_write_reg(REG_FIFO_WR_PTR, 0x00);
    max30102_write_reg(REG_OVF_COUNTER, 0x00);
    max30102_write_reg(REG_FIFO_RD_PTR, 0x00);
}

static void max30102_apply_spo2_config(void)
{
    max30102_write_reg(REG_INTR_ENABLE_1, 0x00);
    max30102_write_reg(REG_INTR_ENABLE_2, 0x00);
    max30102_clear_fifo();
    max30102_write_reg(REG_FIFO_CONFIG, 0x3F);
    max30102_write_reg(REG_SPO2_CONFIG, 0x27);
    max30102_write_reg(REG_LED1_PA, 0x24);
    max30102_write_reg(REG_LED2_PA, 0x24);
    max30102_write_reg(REG_PILOT_PA, 0x7F);
    max30102_write_reg(REG_MODE_CONFIG, 0x03);

    (void)max30102_read_reg(REG_INTR_STATUS_1);
    (void)max30102_read_reg(REG_INTR_STATUS_2);
}

void max30102_init(void)
{
    IICInit(&max30102_bus);
    delay_ms(10);

    max30102_write_reg(REG_MODE_CONFIG, 0x40);
    delay_ms(50);

    max30102_apply_spo2_config();
}

void max30102_read_fifo(void)
{
    uint8_t data[6];

    IIC_Read_Multi_Byte(&max30102_bus, MAX30102_ADDRESS, REG_FIFO_DATA, 6, data);

    fifo_red = ((uint32_t)data[0] << 16 | (uint32_t)data[1] << 8 | data[2]) & 0x03FFFF;
    fifo_ir = ((uint32_t)data[3] << 16 | (uint32_t)data[4] << 8 | data[5]) & 0x03FFFF;
}

void max30102_sleep(bool enable)
{
    if (enable) {
        uint8_t mode = max30102_read_reg(REG_MODE_CONFIG) & 0x07;
        max30102_write_reg(REG_MODE_CONFIG, 0x80 | mode);
        return;
    }

    delay_ms(5);
    max30102_apply_spo2_config();
    delay_ms(10);
}
