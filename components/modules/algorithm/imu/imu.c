#include "imu.h"
#include "math.h"

static float gyroScale = (1.0f / 16.4f) * DEG_TO_RAD;  // 角速度量程250°/s→131，500°/s→65.5，1000°/s→32.8，2000°/s→16.4
static float accelScale = 1.0f / 8192.0f; // 加速度计量程2g→16384，4g→8192，8g→4096，16g→2048

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
    float accMagSq = ax * ax + ay * ay + az * az;
    float alpha = 0.98f; // 默认权重，倾向于陀螺仪

    // 如果加速度模长偏离 1g 太多，说明处于剧动或振动，降低加速度计的可信度
    if (accMagSq > 1.44f || accMagSq < 0.64f) {
        alpha = 0.999f; // 几乎完全信任陀螺仪
    }

    // 4. 互补滤波融合
    // 融合公式: Angle = alpha * (上一次角度 + 陀螺仪积分) + (1 - alpha) * 加速度计角度
    euler->roll  = alpha * (euler->roll + gy * dt) + (1.0f - alpha) * acc_roll;
    euler->pitch = alpha * (euler->pitch + gx * dt) + (1.0f - alpha) * acc_pitch;

    // 5. 航向角处理 (仅通过陀螺仪积分，无磁力计无法纠偏)
    euler->yaw += gz * dt;
}

// 四元素法+动态互补滤波
static void imu_get_angle_plus(Acc_Struct *acc, Gyro_Struct *gyro, EulerAngle_struct *euler, float dt)
{
    static float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f;
    static float integralX = 0.0f, integralY = 0.0f, integralZ = 0.0f;
    static uint16_t boot_count = 0;
    
    float Kp, Ki;

    // 1. 获取校准后的干净数据
    mpu_get_data(acc, gyro);

    // 2. 转换为物理量 (Acc: g, Gyro: rad/s)
    float ax = (float)acc->x * accelScale;
    float ay = (float)acc->y * accelScale;
    float az = (float)acc->z * accelScale;
    float gx = (float)gyro->x * gyroScale;
    float gy = (float)gyro->y * gyroScale;
    float gz = (float)gyro->z * gyroScale;

    // 3. 自适应增益调整
    float accMagSq = ax * ax + ay * ay + az * az;
    if (boot_count < 400) { // 启动快速收敛阶段
        boot_count++;
        Kp = 10.0f; Ki = 0.01f;
    } else {
        // 当剧动时（加速度模长偏离1g较大），降低Kp减小加速度计对姿态的影响
        Kp = (accMagSq > 1.44f || accMagSq < 0.64f) ? 1.2f : 2.5f;
        Ki = 0.001f;
    }

    // 4. 加速度计补偿（仅在非失重/超重过大状态下进行）
    if (accMagSq > 0.01f) {
        float norm = invSqrt(accMagSq);
        ax *= norm; ay *= norm; az *= norm;

        // 估计重力方向 (当前四元数下的重力分量)
        float vx = 2.0f * (q1 * q3 - q0 * q2);
        float vy = 2.0f * (q0 * q1 + q2 * q3);
        float vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;

        // 计算误差（叉乘）
        float ex = (ay * vz - az * vy);
        float ey = (az * vx - ax * vz);
        float ez = (ax * vy - ay * vx);

        // 误差积分
        integralX += ex * Ki * dt;
        integralY += ey * Ki * dt;
        integralZ += ez * Ki * dt;

        // 注入补偿
        gx += Kp * ex + integralX;
        gy += Kp * ey + integralY;
        gz += Kp * ez + integralZ;
    }

    // 5. 四元数微分方程更新
    float qa = q0, qb = q1, qc = q2;
    q0 += (-qb * gx - qc * gy - q3 * gz) * (0.5f * dt);
    q1 += (qa * gx + qc * gz - q3 * gy) * (0.5f * dt);
    q2 += (qa * gy - qb * gz + q3 * gx) * (0.5f * dt);
    q3 += (qa * gz + qb * gy - qc * gx) * (0.5f * dt);

    // 归一化
    float norm = invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q0 *= norm; q1 *= norm; q2 *= norm; q3 *= norm;

    // 6. 转换为欧拉角
    euler->roll  = atan2f(2.0f * (q0 * q1 + q2 * q3), 1.0f - 2.0f * (q1 * q1 + q2 * q2)) * RAD_TO_DEG;
    euler->pitch = asinf(2.0f * (q0 * q2 - q3 * q1)) * RAD_TO_DEG;
    
    // Yaw 角处理（由于没有磁力计，Yaw 会随时间漂移，但在短时间内可参考）
    float current_yaw = atan2f(2.0f * (q0 * q3 + q1 * q2), 1.0f - 2.0f * (q2 * q2 + q3 * q3)) * RAD_TO_DEG;
    
    // 连续 Yaw (Unwrap) 处理逻辑
    static float last_yaw = 0.0f;
    static float yaw_offset = 0.0f;
    float d_yaw = current_yaw - last_yaw;
    if (d_yaw < -180.0f) yaw_offset += 360.0f;
    else if (d_yaw > 180.0f) yaw_offset -= 360.0f;
    
    euler->yaw = current_yaw + yaw_offset;
    last_yaw = current_yaw;
}