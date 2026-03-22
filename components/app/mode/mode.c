#include "mode.h"
#include "key.h"
#include "esp_log.h"
#include "ui.h"

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


