#include "oled.h"
#include "u8g2.h"
#include "bsp_iic.h"
#include "bsp_delay.h"

static iic_bus_t oled_bus = {
    .sda_io = OLED_SDA_PIN,
    .scl_io = OLED_SCL_PIN
};

u8g2_t u8g2 = {0}; 

/**
 * @brief 统一写函数
 * @param data 要写入的数据
 * @param mode 0:命令 / 1:数据
 */
static void OLED_WriteByte(uint8_t data, uint8_t mode)
{
    IICStart(&oled_bus);
    IICSendByte(&oled_bus, (OLED_ADDR << 1)); // 必须左移
    IICWaitAck(&oled_bus);
    
    if (mode == 0) IICSendByte(&oled_bus, 0x00); // 控制字节：命令
    else           IICSendByte(&oled_bus, 0x40); // 控制字节：数据
    
    IICWaitAck(&oled_bus);
    IICSendByte(&oled_bus, data);
    IICWaitAck(&oled_bus);
    IICStop(&oled_bus);
}

// 修改其他的调用
static void OLED_SetCursor(uint8_t Y, uint8_t X)
{
    OLED_WriteByte(0xB0 | Y, 0);                  // 设置Y位置
    OLED_WriteByte(0x10 | ((X & 0xF0) >> 4), 0);  // 设置X位置高4位
    OLED_WriteByte(0x00 | (X & 0x0F), 0);         // 设置X位置低4位
}

static void OLED_Clear(void)
{  
    for (uint8_t j = 0; j < 8; j++)
    {
        OLED_SetCursor(j, 0);
        for(uint8_t i = 0; i < 128; i++)
        {
            OLED_WriteByte(0x00, 1); // 调用统一的写数据
        }
    }
}
void oled_init(void)
{
    delay_ms(5);        
    IICInit(&oled_bus); // 使用通用驱动初始化引脚
    
    // 注意：这里统一使用 OLED_WriteByte，第二个参数为 0 代表命令
    OLED_WriteByte(0xAE, 0); // 关闭显示
    OLED_WriteByte(0xD5, 0); // 设置显示时钟分频
    OLED_WriteByte(0x80, 0);
    OLED_WriteByte(0xA8, 0); // 设置多路复用率
    OLED_WriteByte(0x3F, 0);
    OLED_WriteByte(0xD3, 0); // 设置显示偏移
    OLED_WriteByte(0x00, 0);
    OLED_WriteByte(0x40, 0); // 设置显示开始行
    OLED_WriteByte(0xA1, 0); // 左右方向
    OLED_WriteByte(0xC8, 0); // 上下方向
    OLED_WriteByte(0xDA, 0); // COM引脚配置
    OLED_WriteByte(0x12, 0);
    OLED_WriteByte(0x81, 0); // 对比度
    OLED_WriteByte(0xCF, 0);
    OLED_WriteByte(0xD9, 0); // 预充电周期
    OLED_WriteByte(0xF1, 0);
    OLED_WriteByte(0xDB, 0); // VCOMH级别
    OLED_WriteByte(0x30, 0);
    OLED_WriteByte(0xA4, 0); // 全屏点亮/正常
    OLED_WriteByte(0xA6, 0); // 正常显示
    OLED_WriteByte(0x8D, 0); // 充电泵
    OLED_WriteByte(0x14, 0);
    OLED_WriteByte(0xAF, 0); // 开启显示
        
    OLED_Clear();            // 清屏
}

static uint8_t u8x8_gpio_and_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch(msg)
    {
        case U8X8_MSG_DELAY_MILLI:
            // 使用你 bsp_delay.c 中的毫秒延时
            // delay_ms(arg_int); 
            break;

        case U8X8_MSG_DELAY_I2C:
            // U8G2 软件 I2C 的位宽延时，400KHz 约为 1-2us
            // delay_us(arg_int <= 2 ? 2 : 1); 
            break;
        
        case U8X8_MSG_GPIO_I2C_CLOCK:
            // 调用 bsp_iic.c 中的 SCL 控制
            SCL_Output(&oled_bus, arg_int);
            break;

        case U8X8_MSG_GPIO_I2C_DATA:
            // 调用 bsp_iic.c 中的 SDA 控制
            SDA_Output(&oled_bus, arg_int);
            break;

        default:
            u8x8_SetGPIOResult(u8x8, 1);
            break;
    }
    return 1;
}

/**
 * @brief 在指定位置绘制 32x32 图标
 * @param x 横坐标
 * @param y 纵坐标
 */
void OLED_DrawBluetoothIcon(uint8_t x, uint8_t y)
{
    // 1. 清除内部缓冲区
    u8g2_ClearBuffer(&u8g2); 
    
    // 2. 绘制 XBM 图标
    // 参数含义：结构体, x, y, 宽度, 高度, 位图数组
    u8g2_DrawXBM(&u8g2, x, y, 32, 32, icon_bt); 
    
    // 3. 将缓冲区数据发送到屏幕
    u8g2_SendBuffer(&u8g2); 
}

void u8g2_init(void)
{
    oled_init();
    // 2. 注册 U8G2
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0, u8x8_byte_sw_i2c, u8x8_gpio_and_delay);
    
    // 3. 运行初始化序列
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0); 
    u8g2_ClearBuffer(&u8g2);
    
    u8g2_SetDrawColor(&u8g2, 1); 
    u8g2_SetFont(&u8g2, u8g2_font_6x10_tf); // 设置一个初始字体
}
