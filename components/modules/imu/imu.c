#include "imu.h"
#include "math.h"

/* ============================ 宏定义与全局变量 ================================== */
/* =============================== 变量定义 ===================================== */

/* 弧度转角度常数 */
float RtA = 57.2957795f;   // 弧度 -> 角度
// 陀螺仪量程初始化为 +-2000度/秒: 1/(65536 / 4000) = 0.03051756*2
// float Gyro_G = 0.03051756f * 2;
float Gyro_G = 4000.0 / 65536;   // 度/s
// 度每秒转换为弧度每秒: 2*0.03051756 * 0.0174533f = 0.0005326*2
// float Gyro_Gr = 0.0005326f * 2;
float Gyro_Gr = 4000.0 / 65536 / 180 * 3.1415926;   // 弧度/s
#define squa(Sq) (((float)Sq) * ((float)Sq))        /* 平方计算宏 */

/**
 * @description: 快速平方根倒数算法 1/sqrt(num)
 * @param {float} number
 */
static float Q_rsqrt(float number)
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

static double normAccz; /* z轴方向的加速度 */

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
    volatile struct V
    {
        float x;
        float y;
        float z;
    } Gravity, Acc, Gyro, AccGravity;

    static struct V          GyroIntegError = {0};
    static float             KpDef          = 0.8f;     // 比例增益 (加速计修正权重)
    static float             KiDef          = 0.0003f;  // 积分增益 (误差补偿)
    static Quaternion_Struct NumQ           = {1, 0, 0, 0};
    float                    q0_t, q1_t, q2_t, q3_t;
    // float NormAcc;
    float NormQuat;
    float HalfTime = dt * 0.5f;

    mpu_get_data(gyroAccel);

    // 提取四元数对应的重力分量（姿态阵的第三行）
    Gravity.x = 2 * (NumQ.q1 * NumQ.q3 - NumQ.q0 * NumQ.q2);
    Gravity.y = 2 * (NumQ.q0 * NumQ.q1 + NumQ.q2 * NumQ.q3);
    Gravity.z = 1 - 2 * (NumQ.q1 * NumQ.q1 + NumQ.q2 * NumQ.q2);

    // 加速度计数据归一化
    NormQuat = Q_rsqrt(squa(gyroAccel->acc.x) +
                       squa(gyroAccel->acc.y) +
                       squa(gyroAccel->acc.z));

    Acc.x = gyroAccel->acc.x * NormQuat;
    Acc.y = gyroAccel->acc.y * NormQuat;
    Acc.z = gyroAccel->acc.z * NormQuat;

    // 通过向量外积计算加速度计测得的重力与估计重力的误差
    AccGravity.x = (Acc.y * Gravity.z - Acc.z * Gravity.y);
    AccGravity.y = (Acc.z * Gravity.x - Acc.x * Gravity.z);
    AccGravity.z = (Acc.x * Gravity.y - Acc.y * Gravity.x);

    // 对误差进行积分，补偿陀螺仪的零偏
    GyroIntegError.x += AccGravity.x * KiDef;
    GyroIntegError.y += AccGravity.y * KiDef;
    GyroIntegError.z += AccGravity.z * KiDef;

    // 使用PI补偿后的角速度更新四元数
    Gyro.x = gyroAccel->gyro.x * Gyro_Gr + KpDef * AccGravity.x + GyroIntegError.x;
    Gyro.y = gyroAccel->gyro.y * Gyro_Gr + KpDef * AccGravity.y + GyroIntegError.y;
    Gyro.z = gyroAccel->gyro.z * Gyro_Gr + KpDef * AccGravity.z + GyroIntegError.z;

    // 四元数一阶微分方程更新
    q0_t = (-NumQ.q1 * Gyro.x - NumQ.q2 * Gyro.y - NumQ.q3 * Gyro.z) * HalfTime;
    q1_t = (NumQ.q0 * Gyro.x - NumQ.q3 * Gyro.y + NumQ.q2 * Gyro.z) * HalfTime;
    q2_t = (NumQ.q3 * Gyro.x + NumQ.q0 * Gyro.y - NumQ.q1 * Gyro.z) * HalfTime;
    q3_t = (-NumQ.q2 * Gyro.x + NumQ.q1 * Gyro.y + NumQ.q0 * Gyro.z) * HalfTime;

    NumQ.q0 += q0_t;
    NumQ.q1 += q1_t;
    NumQ.q2 += q2_t;
    NumQ.q3 += q3_t;

    // 四元数单位化归一化
    NormQuat = Q_rsqrt(squa(NumQ.q0) + squa(NumQ.q1) + squa(NumQ.q2) + squa(NumQ.q3));
    NumQ.q0 *= NormQuat;
    NumQ.q1 *= NormQuat;
    NumQ.q2 *= NormQuat;
    NumQ.q3 *= NormQuat;

    /* 计算姿态矩阵中的Z轴分量，用于求取欧拉角 */
    float vecxZ = 2 * NumQ.q0 * NumQ.q2 - 2 * NumQ.q1 * NumQ.q3;     /* 矩阵第(3,1)项 */
    float vecyZ = 2 * NumQ.q2 * NumQ.q3 + 2 * NumQ.q0 * NumQ.q1;     /* 矩阵第(3,2)项 */
    float veczZ = 1 - 2 * NumQ.q1 * NumQ.q1 - 2 * NumQ.q2 * NumQ.q2; /* 矩阵第(3,3)项 */

    if (vecxZ > 1.0f) {
        vecxZ = 1.0f;
    } else if (vecxZ < -1.0f) {
        vecxZ = -1.0f;
    }

    // 航向角计算：Z轴陀螺仪直接积分（由于加速度计无法纠正Z轴漂移）
    float yaw_G = gyroAccel->gyro.z * Gyro_G; 
    if((yaw_G > 0.5f) || (yaw_G < -0.5f)) // 设置死区过滤微小抖动
    {
        eulerAngle->yaw += yaw_G * dt;
    }

    // 俯仰角计算（Pitch）
    eulerAngle->pitch = asin(vecxZ) * RtA;

    // 横滚角计算（Roll）
    eulerAngle->roll = atan2f(vecyZ, veczZ) * RtA;

    // 计算地理坐标系Z轴方向上的实际加速度值
    normAccz = gyroAccel->acc.x * vecxZ + gyroAccel->acc.y * vecyZ + gyroAccel->acc.z * veczZ;
}

/**
 * @description: 获取地理坐标系Z轴方向的加速度分量
 * @return {*}
 */
float imu_get_norm_acc_z(void)
{
    return normAccz;
}
/* ====================== 结束 ================================== */
