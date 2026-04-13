#include "imu.h"

#include <math.h>

/**
 * @description: 快速平方根倒数算法 1/sqrt(num)
 * @param {float} number
 */
static float imu_fast_rsqrt(float number)
{
    long        i;
    float       x2, y;
    const float threehalfs = 1.5F;

    x2 = number * 0.5F;
    y  = number;
    i  = *(long *)&y;
    i  = 0x5f3759df - (i >> 1);
    y  = *(float *)&i;
    y  = y * (threehalfs - (x2 * y * y));   // 1st iteration 第一步牛顿迭代
    return y;
}

/**
 * @description: 获取MPU6050六轴数据，通过互补滤波更新欧拉角
 * @param {GyroAccel_Struct} *gyroAccel mpu6050原始数据结构体
 * @param {EulerAngle_Struct} *EulerAngle 转换后的欧拉角结构体
 * @param {float} dt 采样时间间隔 (单位:s)
 * @return {*}
 */
void imu_get_angle(GyroAccel_Struct  *gyroAccel,
                        EulerAngle_Struct *eulerAngle,
                        float              dt)
{
    typedef struct {
        float x;
        float y;
        float z;
    } imu_vector3_t;

    imu_vector3_t gravity = {0};
    imu_vector3_t acc = {0};
    imu_vector3_t gyro = {0};
    imu_vector3_t acc_gravity = {0};
    static imu_vector3_t s_gyro_integral_error = {0};
    static float s_kp_gain = 0.8f;
    static float s_ki_gain = 0.0003f;
    static imu_quaternion_t s_quaternion = {1.0f, 0.0f, 0.0f, 0.0f};
    float q0_delta;
    float q1_delta;
    float q2_delta;
    float q3_delta;
    float quat_norm;
    float half_dt = dt * 0.5f;
    float vec_x_z;
    float vec_y_z;
    float vec_z_z;
    float yaw_rate_deg;

    mpu_get_data(gyroAccel);

    // 提取四元数对应的重力分量（姿态阵的第三行）
    gravity.x = 2.0f * (s_quaternion.q1 * s_quaternion.q3 - s_quaternion.q0 * s_quaternion.q2);
    gravity.y = 2.0f * (s_quaternion.q0 * s_quaternion.q1 + s_quaternion.q2 * s_quaternion.q3);
    gravity.z = 1.0f - 2.0f * (s_quaternion.q1 * s_quaternion.q1 + s_quaternion.q2 * s_quaternion.q2);

    // 加速度计数据归一化
    quat_norm = imu_fast_rsqrt(imu_squaref(gyroAccel->acc.x) +
                               imu_squaref(gyroAccel->acc.y) +
                               imu_squaref(gyroAccel->acc.z));

    acc.x = gyroAccel->acc.x * quat_norm;
    acc.y = gyroAccel->acc.y * quat_norm;
    acc.z = gyroAccel->acc.z * quat_norm;

    // 通过向量外积计算加速度计测得的重力与估计重力的误差
    acc_gravity.x = (acc.y * gravity.z - acc.z * gravity.y);
    acc_gravity.y = (acc.z * gravity.x - acc.x * gravity.z);
    acc_gravity.z = (acc.x * gravity.y - acc.y * gravity.x);

    // 对误差进行积分，补偿陀螺仪的零偏
    s_gyro_integral_error.x += acc_gravity.x * s_ki_gain;
    s_gyro_integral_error.y += acc_gravity.y * s_ki_gain;
    s_gyro_integral_error.z += acc_gravity.z * s_ki_gain;

    // 使用PI补偿后的角速度更新四元数
    gyro.x = gyroAccel->gyro.x * IMU_GYRO_SCALE_RADPS + s_kp_gain * acc_gravity.x + s_gyro_integral_error.x;
    gyro.y = gyroAccel->gyro.y * IMU_GYRO_SCALE_RADPS + s_kp_gain * acc_gravity.y + s_gyro_integral_error.y;
    gyro.z = gyroAccel->gyro.z * IMU_GYRO_SCALE_RADPS + s_kp_gain * acc_gravity.z + s_gyro_integral_error.z;

    // 四元数一阶微分方程更新
    q0_delta = (-s_quaternion.q1 * gyro.x - s_quaternion.q2 * gyro.y - s_quaternion.q3 * gyro.z) * half_dt;
    q1_delta = (s_quaternion.q0 * gyro.x - s_quaternion.q3 * gyro.y + s_quaternion.q2 * gyro.z) * half_dt;
    q2_delta = (s_quaternion.q3 * gyro.x + s_quaternion.q0 * gyro.y - s_quaternion.q1 * gyro.z) * half_dt;
    q3_delta = (-s_quaternion.q2 * gyro.x + s_quaternion.q1 * gyro.y + s_quaternion.q0 * gyro.z) * half_dt;

    s_quaternion.q0 += q0_delta;
    s_quaternion.q1 += q1_delta;
    s_quaternion.q2 += q2_delta;
    s_quaternion.q3 += q3_delta;

    // 四元数单位化归一化
    quat_norm = imu_fast_rsqrt(imu_squaref(s_quaternion.q0) +
                               imu_squaref(s_quaternion.q1) +
                               imu_squaref(s_quaternion.q2) +
                               imu_squaref(s_quaternion.q3));
    s_quaternion.q0 *= quat_norm;
    s_quaternion.q1 *= quat_norm;
    s_quaternion.q2 *= quat_norm;
    s_quaternion.q3 *= quat_norm;

    /* 计算姿态矩阵中的Z轴分量，用于求取欧拉角 */
    vec_x_z = 2.0f * s_quaternion.q0 * s_quaternion.q2 - 2.0f * s_quaternion.q1 * s_quaternion.q3;
    vec_y_z = 2.0f * s_quaternion.q2 * s_quaternion.q3 + 2.0f * s_quaternion.q0 * s_quaternion.q1;
    vec_z_z = 1.0f - 2.0f * s_quaternion.q1 * s_quaternion.q1 - 2.0f * s_quaternion.q2 * s_quaternion.q2;

    if (vec_x_z > 1.0f) {
        vec_x_z = 1.0f;
    } else if (vec_x_z < -1.0f) {
        vec_x_z = -1.0f;
    }

    // 航向角计算：Z轴陀螺仪直接积分（由于加速度计无法纠正Z轴漂移）
    yaw_rate_deg = gyroAccel->gyro.z * IMU_GYRO_SCALE_DPS;
    if ((yaw_rate_deg > 0.5f) || (yaw_rate_deg < -0.5f)) {
        eulerAngle->yaw += yaw_rate_deg * dt;
    }

    // 俯仰角计算（Pitch）
    eulerAngle->pitch = asinf(vec_x_z) * IMU_RAD_TO_DEG;

    // 横滚角计算（Roll）
    eulerAngle->roll = atan2f(vec_y_z, vec_z_z) * IMU_RAD_TO_DEG;
}
