#ifndef __BLOOD_H
#define __BLOOD_H

#include <stdint.h>

#define FFT_N           512     
#define START_INDEX     4       

/* 统一使用这种定义方式 */
typedef struct {
    float real;
    float imag;
} compx;

//向下取整
double my_floor(double x);
//求余运算
double my_fmod(double x, double y);
//正弦函数
double XSin( double x );
//余弦函数 
double XCos( double x );
//开平方
int qsqrt(int a);

/*******************************************************************
函数原型：struct compx EE(struct compx b1,struct compx b2)  
函数功能：对两个复数进行乘法运算
输入参数：两个以联合体定义的复数a,b
输出参数：a和b的乘积，以联合体的形式输出
*******************************************************************/
compx EE(compx a, compx b);
/*****************************************************************
函数原型：void FFT(struct compx *xin,int N)
函数功能：对输入的复数组进行快速傅里叶变换（FFT）
输入参数：*xin复数结构体组的首地址指针，struct型
*****************************************************************/
void FFT(compx *xin);

typedef struct {
    int heart;      
    float SpO2;     
} BloodData_t;

extern BloodData_t b_data;

void BloodDataUpdate(void);
void BloodDataTranslate(void);
int find_max_num_index(compx *data, int count);

#endif