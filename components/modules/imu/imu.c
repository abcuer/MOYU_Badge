#include "imu.h"
#include "math.h"

// static float gyroScale = (1.0f / 16.4f) * DEG_TO_RAD;  // 角速度量程250°/s→131，500°/s→65.5，1000°/s→32.8，2000°/s→16.4
// static float accelScale = 1.0f / 8192.0f; // 加速度计量程2g→16384，4g→8192，8g→4096，16g→2048

// 快速平方根倒数算法
static inline float invSqrt(float x)
{
    float halfx = 0.5f * x;
    float y     = x;
    long i      = *(long *)&y;
    i           = 0x5f3759df - (i >> 1);
    y           = *(float *)&i;
    y           = y * (1.5f - (halfx * y * y)); // 一次牛顿迭代
    return y;
}

void imu_get_angle(Acc_Struct *acc, Gyro_Struct *gyro, EulerAngle_struct *euler, float dt)
{
    // 0. 获取校准后的干净数据
    mpu_get_data(acc, gyro);
    // 1. 转换为物理量
    // 加速度单位: g, 陀螺仪单位: °/s
    float ax = (float)acc->x * 1.0f / 8192.0f; 
    float ay = (float)acc->y * 1.0f / 8192.0f;
    float az = (float)acc->z * 1.0f / 8192.0f;
    
    float gx = (float)gyro->x * 1.0f / 16.4f;
    float gy = (float)gyro->y * 1.0f / 16.4f;
    float gz = (float)gyro->z * 1.0f / 16.4f;

    // 2. 计算加速度计得到的姿态角 (静止时的参考值)
    // 使用 atan2f 提高效率和精度
    float acc_roll  = atan2f(ay, az) * 57.29578f;
    float acc_pitch = -atan2f(ax, sqrtf(ay * ay + az * az)) * 57.29578f;

    // 3. 动态权重计算 (自适应互补滤波)
    // float accMagSq = ax * ax + ay * ay + az * az;
    float alpha = 0.55f; 

    // 4. 互补滤波融合
    // 融合公式: Angle = alpha * (上一次角度 + 陀螺仪积分) + (1 - alpha) * 加速度计角度
    euler->roll  = alpha * (euler->roll + gy * dt) + (1.0f - alpha) * acc_roll;
    euler->pitch = alpha * (euler->pitch + gx * dt) + (1.0f - alpha) * acc_pitch;

    // 5. 航向角处理 (仅通过陀螺仪积分，无磁力计无法纠偏)
    euler->yaw += gz * dt;
}