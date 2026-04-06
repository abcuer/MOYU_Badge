#ifndef __IMU_H
#define __IMU_H

#include "mpu6050.h"

// 四元数结构体
typedef struct
{
	float q0;
	float q1;
	float q2;
	float q3;	
}Quaternion_Struct;

/**
 * @brief 更新欧拉角函数
 */
void imu_get_angle(GyroAccel_Struct  *gyroAccel,
                              EulerAngle_Struct *eulerAngle,
                              float              dt);

/**
 * @brief 获取归一化后的Z轴加速度
 */
float imu_get_norm_acc_z(void);

/* 欧拉角计算用到的全局变量 */
extern float RtA;   // 弧度 -> 角度

// 陀螺仪量程初始化为 +-2000度/秒: 1/(65536 / 4000) = 0.03051756*2
extern float Gyro_G;   // 度/s

// 度每秒转换为弧度每秒: 2*0.03051756 * 0.0174533f = 0.0005326*2
extern float Gyro_Gr;   // 弧度/s

#endif