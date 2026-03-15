#include "math.h"
#include "cJSON.h"          // <--- 必须移到最前面，至少要在 onenet_dm.h 之前
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h" // 补上事件组头文件

#include "user_task.h"
#include "max30102.h"
#include "imu.h"
#include "blood.h"
#include "wifi_manager.h"
#include "onenet_mqtt.h"
#include "onenet_dm.h"

#define MPU_PERIOD      50
#define MAX30102_PERIOD  50
#define OLED_PERIOD     50
#define onenet_PERIOD   1000

void start_mpu_task(void *p)
{
    static bool low_g_flag = false;
    static bool impact_flag = false;
    while(1)
    {
        imu_get_angle(&acc, &gyro, &euler_angle, MPU_PERIOD/1000.0f);
    
        // 1. 计算合加速度
        float svm = sqrt(acc.x*acc.x + acc.y*acc.y + acc.z*acc.z);
        
        // 2. 状态机判断
        if (svm < 0.5f) {
            low_g_flag = true; // 检测到失重
        }
        
        if (low_g_flag && svm > 2.5f) {
            impact_flag = true; // 检测到撞击
        }
        
        if (impact_flag) {
            vTaskDelay(pdMS_TO_TICKS(1500)); // 等待 1.5 秒观察最终姿态
            
            // 3. 结合欧拉角判断是否横卧 (Pitch 或 Roll 接近 90 度)
            if (fabs(euler_angle.pitch) > 60 || fabs(euler_angle.roll) > 60) {
                // onenet_send_emergency_msg("User Fell Down!"); // 立即上报云端
                
                // 重置标志位
                low_g_flag = false;
                impact_flag = false;
            }
        }
    }


}

void start_detect_task(void *p)
{
    while(1)
    {
        BloodDataUpdate();     // 采集 512 个点
        BloodDataTranslate();  // 算法处理
        vTaskDelay(pdMS_TO_TICKS(MAX30102_PERIOD)); 
    }
}

void onenet_upload_task(void *pvParameters) 
{
    xEventGroupWaitBits(wifi_ev, WIFI_CONNECT_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    onenet_start();
    // 启动 OneNET MQTT 连接
    onenet_start();

    while (1) 
    {
        // 1. 读取传感器并生成 JSON
        cJSON *prop_json = onenet_property_upload_dm();
        
        if (prop_json != NULL) 
        {
            char *post_data = cJSON_PrintUnformatted(prop_json);
            
            // 2. 上报数据
            onenet_post_property_data(post_data);

            // 3. 必须释放内存
            cJSON_free(post_data);
            cJSON_Delete(prop_json);
        }

        vTaskDelay(pdMS_TO_TICKS(onenet_PERIOD));
    }
}
