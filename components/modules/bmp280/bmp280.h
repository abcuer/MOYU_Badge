#ifndef __BMP280_H
#define __BMP280_H

#include <stdint.h>
#include <stdbool.h>

#define BMP_SDA_PIN         45
#define BMP_SCL_PIN         0

#define BMP_IIC_BUS         0
#define BMP280_ADDR         0x76

#define BMP280_REG_CALIB    0x88  // 补偿参数起始地址
#define BMP280_REG_CTRL     0xF4
#define BMP280_REG_DATA     0xF7

// 补偿参数结构体
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
} bmp280_calib_t;

typedef struct {
    float temperature;
    float pressure;
    float altitude;
} bmp280_data_t;

void bmp280_init(void);
void bmp280_sleep(bool enable);
void bmp280_read_data(bmp280_data_t *bmp280);

extern bmp280_data_t bmp280;

#endif