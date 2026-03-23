#include "mpu6050.h"
#include "bsp_delay.h"
#include "driver/i2c_master.h"

Acc_Struct acc;
Gyro_Struct gyro;
EulerAngle_struct euler_angle;

static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t mpu6050_handle;

static Gyro_Struct gyro_offsets = {0};
static Acc_Struct acc_offsets = {0};

static void iic_init(void) 
{
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = MPU_IIC_BUS,
        .scl_io_num = MPU_SCL_PIN,
        .sda_io_num = MPU_SDA_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_new_master_bus(&bus_config, &bus_handle);

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU6050_ADDR,
        .scl_speed_hz = 100000, // 100kHz 快速模式
    };
    i2c_master_bus_add_device(bus_handle, &dev_config, &mpu6050_handle);
}

static esp_err_t MPU6050_Write_Reg(uint8_t reg_addr, uint8_t data) 
{
    uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_transmit(mpu6050_handle, write_buf, sizeof(write_buf), -1);
}


static void mpu_read_raw_data(Acc_Struct *acc, Gyro_Struct *gyro) 
{
    uint8_t data[14];
    uint8_t reg_addr = 0x3B; 

    esp_err_t ret = i2c_master_transmit_receive(mpu6050_handle, &reg_addr, 1, data, 14, -1);
    
    if (ret == ESP_OK) 
    {
        // 使用 -> 访问结构体指针成员
        acc->x = (int16_t)((data[0] << 8) | data[1]);
        acc->y = (int16_t)((data[2] << 8) | data[3]);
        acc->z = (int16_t)((data[4] << 8) | data[5]);

        gyro->x = (int16_t)((data[8] << 8) | data[9]);
        gyro->y = (int16_t)((data[10] << 8) | data[11]);
        gyro->z = (int16_t)((data[12] << 8) | data[13]);
    }
}

/**
 * @brief 零偏校准函数
 * @note  调用此函数时，必须保证设备处于水平且绝对静止状态
 */
static void mpu_calibrate(void)
{
    Acc_Struct tmp_acc;
    Gyro_Struct tmp_gyro;
    
    int32_t sum_ax = 0, sum_ay = 0, sum_az = 0;
    int32_t sum_gx = 0, sum_gy = 0, sum_gz = 0;

    for (int i = 0; i < CALIBRATION_SAMPLES; i++)
    {
        mpu_read_raw_data(&tmp_acc, &tmp_gyro); // 读取原始值
        
        sum_ax += tmp_acc.x;
        sum_ay += tmp_acc.y;
        sum_az += tmp_acc.z;
        
        sum_gx += tmp_gyro.x;
        sum_gy += tmp_gyro.y;
        sum_gz += tmp_gyro.z;

        delay_ms(5); // 配合采样率 (200Hz)
    }

    // 计算平均偏移量
    acc_offsets.x = sum_ax / CALIBRATION_SAMPLES;
    acc_offsets.y = sum_ay / CALIBRATION_SAMPLES;
    // 加速度计 Z 轴注意：水平放置时 Z 轴应感应到 1g (当前配置下 4g 对应 8192 LSB)
    // 如果不需要减去重力，则直接 sum_az / CALIBRATION_SAMPLES
    acc_offsets.z = (sum_az / CALIBRATION_SAMPLES) - 8192; 

    gyro_offsets.x = sum_gx / CALIBRATION_SAMPLES;
    gyro_offsets.y = sum_gy / CALIBRATION_SAMPLES;
    gyro_offsets.z = sum_gz / CALIBRATION_SAMPLES;
}

void mpu6050_init(void) 
{
    iic_init();
    // 1. 电源管理：唤醒并设置时钟源为 PLL X轴
    MPU6050_Write_Reg(0x6B, 0x01);
    delay_ms(100);
    // 2. SMPLRT_Rate = 200Hz
    // 采样率 = 1000Hz / (1 + SMPLRT_DIV), 所以 DIV = 4
    MPU6050_Write_Reg(0x19, 0x04);
    // 3. Filter = Band_21Hz (DLPF_CFG = 4)
    MPU6050_Write_Reg(0x1A, 0x04);
    // 4. gyro_range = gyro_2000 (FS_SEL = 3 -> 0x18)
    MPU6050_Write_Reg(0x1B, 0x18);
    // 5. acc_range = acc_4g (AFS_SEL = 1 -> 0x08)
    MPU6050_Write_Reg(0x1C, 0x08);
    // 6. INT = Data_Ready_EN (关闭数据就绪中断)
    MPU6050_Write_Reg(0x38, 0x00);
    delay_ms(2);
    mpu_calibrate();
}

/**
 * @brief 获取校准后的数据
 */
void mpu_get_data(Acc_Struct *acc, Gyro_Struct *gyro)
{
    mpu_read_raw_data(acc, gyro); // 先读原始数据

    // 减去零偏
    acc->x -= acc_offsets.x;
    acc->y -= acc_offsets.y;
    acc->z -= acc_offsets.z;

    gyro->x -= gyro_offsets.x;
    gyro->y -= gyro_offsets.y;
    gyro->z -= gyro_offsets.z;
}