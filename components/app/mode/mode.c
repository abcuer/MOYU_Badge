#include "headfile.h"

// 是否处于"选择模式"（覆盖在当前界面上）
bool in_select = false;
// 记录最后操作时间（毫秒）
uint32_t last_action_time = 0; 

extern TaskHandle_t sensor_task_handle;
extern TaskHandle_t sync_task_handle;

void key_scan(void)
{
    key_event_e event = key_get_event(KEY_USER);

    if (event != KEY_EVENT_NONE) {
        // 🚀 只要触发了短按或者长按，重置倒计时
        last_action_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }

    const int main_app_count = sizeof(main_app_list) / sizeof(main_app_list[0]);
    const int sub_game_count = sizeof(sub_game_list) / sizeof(sub_game_list[0]);

    if (event == KEY_EVENT_SHORT)
    {
        if (in_select)
        {
            // 🎯 【短按切页】：直接根据 menu_layer 锁定列表，绝不会跑偏！
            if (menu_layer == 2) {
                int idx = 0;
                for(int i = 0; i < sub_game_count; i++) {
                    if(sub_game_list[i] == selected_game) { idx = i; break; }
                }
                idx = (idx + 1) % sub_game_count;
                selected_game = sub_game_list[idx];
            } 
            else {
                int idx = 0;
                for(int i = 0; i < main_app_count; i++) {
                    if(main_app_list[i] == selected_game) { idx = i; break; }
                }
                idx = (idx + 1) % main_app_count;
                selected_game = main_app_list[idx];
            }
        }
        else
        {
            if (mode == MODE_DINO && dino_game.state == STATE_GAMEOVER) {
                dino_game_reset(&dino_game); 
            }
            else if (mode == MODE_PLANE && air_game.state == STATE_GAMEOVER) {
                air_game_reset(&air_game);
            }
        }
    }
    else if (event == KEY_EVENT_LONG)
    {
        if (!in_select)
        {
            selected_game = mode; 
            in_select = true;
            
            // 🎯 重新进入选择列表时，判定它应该处于什么层级
            if (mode == MODE_BALL || mode == MODE_DINO || mode == MODE_PLANE) {
                menu_layer = 2; // 如果是从游戏退出来的，锁定在二级菜单层级
            } else {
                menu_layer = 1; // 如果是从普通App退出来的，锁定在一级菜单层级
            }
            return;
        }

        if (in_select)
        {
            // 🎯 【长按处理】：根据 menu_layer 严丝合缝进行深钻/跳出
            if (menu_layer == 2) {
                if (selected_game == MODE_GAME_SELECT) {
                    // 长按 Back 键：降级为一级菜单，并指向 Game 大厅
                    menu_layer = 1;
                    selected_game = MODE_GAME_SELECT; 
                } else {
                    // 开启具体物理游戏
                    mode = selected_game;
                    in_select = false;
                }
            } 
            else { // menu_layer == 1
                if (selected_game == MODE_GAME_SELECT) {
                    // 长按 Game 键：升级为二级菜单，并指向 Ball
                    menu_layer = 2;
                    selected_game = MODE_BALL; 
                } else {
                    // 开启普通 App (Clock, Setting, Blood)
                    mode = selected_game;
                    in_select = false;
                }
            }
            return;
        }
    }
}

// mode.c
void enter_light_sleep(void)
{
    u8g2_SetPowerSave(&u8g2, 1); // 息屏
    mpu6050_sleep(1);
    bmp280_sleep(1);
    max30102_sleep(1);
    if (sensor_task_handle != NULL) {
        vTaskSuspend(sensor_task_handle); // 暂停传感器 I2C 轮询
    }

    if (sync_task_handle != NULL) {
        vTaskSuspend(sync_task_handle); 
    }

    // 🎯 【修正 1】：放弃 ext0，改用 ESP32-S3 官方最推荐的 Light-sleep GPIO 唤醒方式
    gpio_wakeup_enable(USER_KEY_PIN, GPIO_INTR_LOW_LEVEL); // 低电平唤醒
    esp_sleep_enable_gpio_wakeup(); 

    // 🎯 【修正 2】：睡眠前，强行将按键状态机复位挂起，防止带着“按下”的脏数据去睡觉导致秒醒
    key_reset_fsm(KEY_USER);

    esp_light_sleep_start(); // 💤 真正的睡觉阻塞点 💤

    // 🚀🚀 按下按键瞬间，从这里苏醒 🚀🚀

    u8g2_SetPowerSave(&u8g2, 0); // 亮屏
    mpu6050_sleep(0);
    bmp280_sleep(0);
    max30102_sleep(0);

    if (sensor_task_handle != NULL) {
        vTaskResume(sensor_task_handle); // 恢复传感器
    }
    if (sync_task_handle != NULL) {
        vTaskResume(sync_task_handle); 
    }

    // 🎯 【修正 3】：苏醒后单方面宣布退出选择页面，重置按键状态机
    in_select = false; 
    key_reset_fsm(KEY_USER);

    // 🎯 【修正 4】：苏醒瞬间刷新时间戳！
    last_action_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // 唤醒后，关闭该引脚的唤醒功能，防止平时运行时干扰正常中断
    gpio_wakeup_disable(USER_KEY_PIN); 
}

// 滑动平均滤波（平滑SVM，减少毛刺）
#define SVM_BUF_SIZE 5

static StepFSM_t  fsm_state    = STEP_STATE_IDLE;
static uint32_t   last_step_tick = 0;   // 上一步的时间戳

static float svm_buf[SVM_BUF_SIZE] = {0};
static int   svm_idx = 0;

StepData_t step_data = {0};

static float svm_smooth(float new_val)
{
    svm_buf[svm_idx] = new_val;
    // 限制 svm_idx 永远在 [0, SVM_BUF_SIZE - 1] 之间，绝不溢出
    svm_idx = (svm_idx + 1) % SVM_BUF_SIZE; 

    float sum = 0;
    for (int i = 0; i < SVM_BUF_SIZE; i++) sum += svm_buf[i];
    return sum / SVM_BUF_SIZE;
}

void step_detect(void)
{
    mpu_get_data(&acc, &gyro);
    
    float svm = sqrtf(acc.x*acc.x + acc.y*acc.y + acc.z*acc.z) / 16384.0f;
    float svm_f = svm_smooth(svm);
    ESP_LOGI("SVM", "SVM: %.2f", svm_f);
    ESP_LOGI("SVM", "STEP: %u", (unsigned int)step_data.today_steps);
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    switch (fsm_state)
    {
        case STEP_STATE_IDLE:
            if (svm_f > STEP_THRESHOLD_HIGH) {
                fsm_state = STEP_STATE_HIGH;
            }
            break;

        case STEP_STATE_HIGH:
            if (svm_f < STEP_THRESHOLD_LOW){ 
                uint32_t interval = now - last_step_tick;

                // 1. 只有开机第一次，或者间隔时间在正常人类迈步范围内(如 200ms ~ 2000ms)
                if (last_step_tick == 0 || (interval > STEP_MIN_INTERVAL_MS && interval < STEP_MAX_INTERVAL_MS))
                {
                    step_data.total_steps++;
                    step_data.today_steps++;

                    if (last_step_tick > 0) {
                        step_data.cadence = 60000.0f / interval;
                        step_data.is_walking = true;
                    }
                    
                    last_step_tick = now; // ✅ 只有计步成功，才把这步作为“下一步的基准”
                }
                // 2. 如果甩得太快，判定为杂波噪声，直接忽略，【千万不要更新 last_step_tick】
                
                fsm_state = STEP_STATE_IDLE; 
            }
            // 超时检测：如果卡在高电平太久(比如静止不动了)，强制踢回 IDLE
            else if (now - last_step_tick > STEP_MAX_INTERVAL_MS) {
                fsm_state = STEP_STATE_IDLE;
            }
            break;
    }

    // 超过2秒没动静，清除走路标志
    if (now - last_step_tick > STEP_MAX_INTERVAL_MS) {
        step_data.is_walking = false;
        step_data.cadence    = 0;
    }
}