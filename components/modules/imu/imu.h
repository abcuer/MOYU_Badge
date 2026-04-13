#ifndef __IMU_H
#define __IMU_H

#include <stdint.h>

#include "mpu6050.h"

typedef struct {
    float q0;
    float q1;
    float q2;
    float q3;
} imu_quaternion_t;

#define IMU_RAD_TO_DEG       57.2957795f
#define IMU_GYRO_SCALE_DPS   (4000.0f / 65536.0f)
#define IMU_GYRO_SCALE_RADPS (IMU_GYRO_SCALE_DPS / 180.0f * 3.1415926f)

static inline float imu_squaref(float value)
{
    return value * value;
}

/**
 * @brief 更新欧拉角函数
 */
void imu_get_angle(GyroAccel_Struct  *gyroAccel,
                              EulerAngle_Struct *eulerAngle,
                              float              dt);

#endif
