#include "headfile.h"

// 是否处于"选择模式"（覆盖在当前界面上）
bool in_select = false;

void key_scan(void)
{
    key_event_e event = key_get_event(KEY_USER);

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
                    ESP_LOGI("KEY", "二级菜单长按返回，回到一级 App 菜单");
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
                    ESP_LOGI("KEY", "进入二级游戏选择菜单");
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
void step_reset_today(void)
{
    step_data.today_steps = 0;
}