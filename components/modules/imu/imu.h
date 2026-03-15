#ifndef __IMU_H
#define __IMU_H
#include "stdint.h"
#include "mpu6050.h"

#define PI          3.14159265f
#define RAD_TO_DEG  57.2957795f
#define DEG_TO_RAD  0.01745329f

void imu_get_angle(Acc_Struct *acc, Gyro_Struct *gyro, EulerAngle_struct *euler, float dt);

#endif