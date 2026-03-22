#include "headfile.h"

static const int game_count = 5;
// 是否处于"选择模式"（覆盖在当前界面上）
bool in_select = false;

void key_scan(void)
{
    key_event_e event = key_get_event(KEY_USER);

    if (event == KEY_EVENT_SHORT)
    {
        if (in_select)
        {
            // 选择模式下短按 → 切换到下一个
            selected_game++;
            if (selected_game >= game_count)
                selected_game = 0;
            ESP_LOGI("KEY", "选中: %d", game_list[selected_game]);
        }
        else
        {
            if (mode == MODE_DINO) {
                if (dino_game.state == STATE_GAMEOVER) {
                    // 传入结构体地址进行重置
                    dino_game_reset(&dino_game); 
                }
            }
            else if(mode == MODE_PLANE)
            {
                if (air_game.state == STATE_GAMEOVER) {
                    // 传入结构体地址进行重置
                    air_game_reset(&air_game);
                }
            }
        }
    }
    else if (event == KEY_EVENT_LONG)
    {
        if (!in_select)
        {
            // 任意界面长按 → 进入选择模式
            // 默认选中当前模式，方便用户知道自己在哪
            for (int i = 0; i < game_count; i++) {
                if (game_list[i] == mode) {
                    selected_game = i;
                    break;
                }
            }
            in_select = true;
            ESP_LOGI("KEY", "进入选择模式，当前: %d", mode);
        }
        else
        {
            // 选择模式下长按 → 确认进入选中的模式
            mode = game_list[selected_game];
            in_select = false;
            ESP_LOGI("KEY", "进入模式: %d", mode);
        }
    }
}

static StepFSM_t  fsm_state    = STEP_STATE_IDLE;
static uint32_t   last_step_tick = 0;   // 上一步的时间戳

static float svm_buf[SVM_BUF_SIZE] = {0};
static int   svm_idx = 0;

static float svm_smooth(float new_val)
{
    svm_buf[svm_idx % SVM_BUF_SIZE] = new_val;
    svm_idx++;
    float sum = 0;
    for (int i = 0; i < SVM_BUF_SIZE; i++) sum += svm_buf[i];
    return sum / SVM_BUF_SIZE;
}

StepData_t step_data = {0};
void step_detect(Acc_Struct *acc)
{
    // 1. 计算合加速度（单位：g）
    float svm = sqrtf(acc->x*acc->x + acc->y*acc->y + acc->z*acc->z);

    // 2. 滑动平均滤波
    float svm_f = svm_smooth(svm);

    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    switch (fsm_state)
    {
        case STEP_STATE_IDLE:
            // SVM 超过高阈值，进入 HIGH 状态
            if (svm_f > STEP_THRESHOLD_HIGH) {
                fsm_state = STEP_STATE_HIGH;
            }
            break;

        case STEP_STATE_HIGH:
            // SVM 下降到低阈值以下，认为完成一步
            if (svm_f < STEP_THRESHOLD_LOW)
            {
                uint32_t interval = now - last_step_tick;

                // 防抖：间隔在合理范围内才计步
                if (last_step_tick == 0 ||
                   (interval > STEP_MIN_INTERVAL_MS &&
                    interval < STEP_MAX_INTERVAL_MS))
                {
                    step_data.total_steps++;
                    step_data.today_steps++;

                    // 计算步频（bpm）
                    if (last_step_tick > 0 && interval < STEP_MAX_INTERVAL_MS) {
                        step_data.cadence = 60000.0f / interval;
                        step_data.is_walking = true;
                    }
                    last_step_tick = now;
                }
                fsm_state = STEP_STATE_IDLE;
            }
            break;
    }

    // 超过2秒没有新步伐，认为停止走路
    if (last_step_tick > 0 && (now - last_step_tick) > STEP_MAX_INTERVAL_MS) {
        step_data.is_walking = false;
        step_data.cadence    = 0;
    }
}

void step_reset_today(void)
{
    step_data.today_steps = 0;
}