#include "bmp280.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#define TAG "BMP280"

bmp280_data_t bmp280 = {0};
i2c_master_dev_handle_t dev_handle;
bmp280_calib_t calib_data;
int32_t t_fine; // 用于压力计算的中间变量

/**
 * @brief 读取传感器出厂补偿参数
 */
static void bmp280_get_calib_params(void) 
{
    uint8_t reg_addr = BMP280_REG_CALIB;
    uint8_t data[24];
    
    ESP_ERROR_CHECK(i2c_master_transmit_receive(dev_handle, &reg_addr, 1, data, 24, -1));

    calib_data.dig_T1 = (data[1] << 8) | data[0];
    calib_data.dig_T2 = (data[3] << 8) | data[2];
    calib_data.dig_T3 = (data[5] << 8) | data[4];
    calib_data.dig_P1 = (data[7] << 8) | data[6];
    calib_data.dig_P2 = (data[9] << 8) | data[8];
    calib_data.dig_P3 = (data[11] << 8) | data[10];
    calib_data.dig_P4 = (data[13] << 8) | data[12];
    calib_data.dig_P5 = (data[15] << 8) | data[14];
    calib_data.dig_P6 = (data[17] << 8) | data[16];
    calib_data.dig_P7 = (data[19] << 8) | data[18];
    calib_data.dig_P8 = (data[21] << 8) | data[20];
    calib_data.dig_P9 = (data[23] << 8) | data[22];
}

/**
 * @brief 补偿温度计算 (单位: ℃)
 */
static float bmp280_compensate_T(int32_t adc_T) 
{
    int32_t var1, var2;
    var1 = ((((adc_T >> 3) - ((int32_t)calib_data.dig_T1 << 1))) * ((int32_t)calib_data.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)calib_data.dig_T1)) * ((adc_T >> 4) - ((int32_t)calib_data.dig_T1))) >> 12) * ((int32_t)calib_data.dig_T3)) >> 14;
    t_fine = var1 + var2;
    return (float)((t_fine * 5 + 128) >> 8) / 100.0;
}

/**
 * @brief 补偿压力计算 (单位: hPa)
 */
static float bmp280_compensate_P(int32_t adc_P) 
{
    int64_t var1, var2, p;
    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)calib_data.dig_P6;
    var2 = var2 + ((var1 * (int64_t)calib_data.dig_P5) << 17);
    var2 = var2 + (((int64_t)calib_data.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)calib_data.dig_P3) >> 8) + ((var1 * (int64_t)calib_data.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)calib_data.dig_P1) >> 33;

    if (var1 == 0) return 0; // 防止除以零

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)calib_data.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)calib_data.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)calib_data.dig_P7) << 4);
    return (float)p / 25600.0;
}

/**
 * @brief 初始化 I2C 总线和设备
 */
void bmp280_init(void) 
{
    // 1. 配置总线
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BMP_SDA_PIN,
        .scl_io_num = BMP_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, 
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    // 2. 添加设备到总线
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BMP280_ADDR,
        .scl_speed_hz = 400 * 1000, // 400kHz
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));

    // 获取补偿参数
    bmp280_get_calib_params();

    // 3. 配置 BMP280 (写 0xF4 设置为 Normal Mode)
    uint8_t config_data[] = {BMP280_REG_CTRL, 0x27}; // 压力x1, 温度x1, Normal模式
    ESP_ERROR_CHECK(i2c_master_transmit(dev_handle, config_data, sizeof(config_data), -1));
}

/**
 * @brief 读取最终转换后的数值
 */
void bmp280_read_data(bmp280_data_t *bmp280) 
{
    uint8_t reg_addr = BMP280_REG_DATA;
    uint8_t data[6];
    
    if (i2c_master_transmit_receive(dev_handle, &reg_addr, 1, data, 6, -1) == ESP_OK) 
    {
        // 合成原始 ADC 值
        int32_t adc_P = (data[0] << 12) | (data[1] << 4) | (data[2] >> 4);
        int32_t adc_T = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4);

        // 转换数值
        bmp280->temperature = bmp280_compensate_T(adc_T);
        bmp280->pressure = bmp280_compensate_P(adc_P);
        
        // ESP_LOGI(TAG, "Temp: %.2f C, Pres: %.2f hPa", bmp280->temperature, bmp280->pressure);
    }
}