#ifndef __MPU6050_H
#define __MPU6050_H

#include "stdint.h"

#define MPU_SDA_PIN           7     
#define MPU_SCL_PIN           6   
// 零偏校准次数
#define CALIBRATION_SAMPLES 200


#define I2C_MASTER_NUM        0      // I2C 端口号
#define MPU6050_ADDR          0x68   // MPU6050 地址

typedef struct{
    int16_t x;
    int16_t y;
    int16_t z;    
}Acc_Struct;

typedef struct{
    int16_t x;
    int16_t y;
    int16_t z;    
}Gyro_Struct;

typedef struct{
    float pitch;
    float roll;
    float yaw;
}EulerAngle_struct;

void mpu_init(void);
void mpu_get_data(Acc_Struct *acc, Gyro_Struct *gyro);

extern Acc_Struct acc;
extern Gyro_Struct gyro;
extern EulerAngle_struct euler_angle;

#endif 
