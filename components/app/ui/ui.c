
#include "headfile.h"
      
ui_mode_e mode = MODE_CLOCK;
ui_mode_e selected_game = MODE_BALL;
static bool setting_wifi_reset_armed = false;
static setting_item_t s_setting_item = SETTING_ITEM_INFO;
typedef enum {
    SETTING_PAGE_MENU = 0,
    SETTING_PAGE_INFO,
    SETTING_PAGE_VOLUME,
    SETTING_PAGE_WIFI_RESET,
} setting_page_t;
static setting_page_t s_setting_page = SETTING_PAGE_MENU;
static bool s_setting_volume_editing = false;
static uint8_t s_setting_preview_volume = SETTINGS_DEFAULT_VOLUME;
static int8_t s_setting_tilt_state = 0;

#define SETTING_VOLUME_STEP          1
#define SETTING_TILT_TRIGGER_DEG     12.0f
#define SETTING_TILT_NEUTRAL_DEG     5.0f

static void setting_ui_apply_preview_volume(void)
{
    settings_set_volume(s_setting_preview_volume);
    audio_player_set_volume(s_setting_preview_volume);
}

void setting_ui_reset_state(void)
{
    setting_wifi_reset_armed = false;
    s_setting_item = SETTING_ITEM_INFO;
    s_setting_page = SETTING_PAGE_MENU;
    s_setting_volume_editing = false;
    s_setting_preview_volume = settings_get_volume();
    s_setting_tilt_state = 0;
}

bool setting_ui_handle_short_press(void)
{
    if (s_setting_page != SETTING_PAGE_MENU) {
        return true;
    }

    setting_wifi_reset_armed = false;
    s_setting_item = (setting_item_t)((s_setting_item + 1) % 4);
    return true;
}

bool setting_ui_handle_long_press(void)
{
    if (s_setting_page == SETTING_PAGE_INFO) {
        s_setting_page = SETTING_PAGE_MENU;
        return true;
    }

    if (s_setting_page == SETTING_PAGE_VOLUME) {
        settings_set_volume(s_setting_preview_volume);
        settings_save_volume();
        audio_player_set_volume(s_setting_preview_volume);
        s_setting_page = SETTING_PAGE_MENU;
        s_setting_volume_editing = false;
        s_setting_tilt_state = 0;
        return true;
    }

    if (s_setting_page == SETTING_PAGE_WIFI_RESET) {
        u8g2_ClearBuffer(&u8g2);
        u8g2_SetFont(&u8g2, u8g2_font_wqy12_t_gb2312);
        u8g2_DrawUTF8(&u8g2, 18, 28, "正在清除 Wi-Fi");
        u8g2_DrawUTF8(&u8g2, 18, 46, "设备即将重启");
        u8g2_SendBuffer(&u8g2);
        erase_wifi_from_nvs();
        esp_restart();
        return true;
    }

    if (s_setting_item == SETTING_ITEM_INFO) {
        s_setting_page = SETTING_PAGE_INFO;
        return true;
    }

    if (s_setting_item == SETTING_ITEM_VOLUME) {
        s_setting_preview_volume = settings_get_volume();
        audio_player_set_volume(s_setting_preview_volume);
        s_setting_page = SETTING_PAGE_VOLUME;
        s_setting_volume_editing = true;
        s_setting_tilt_state = 0;
        last_action_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        return true;
    }

    if (s_setting_item == SETTING_ITEM_WIFI_RESET) {
        s_setting_page = SETTING_PAGE_WIFI_RESET;
        return true;
    }

    if (s_setting_item == SETTING_ITEM_EXIT) {
        return false;
    }

    return false;
}

bool setting_ui_is_volume_editing(void)
{
    return s_setting_volume_editing;
}

void setting_ui_update_volume_tilt(float roll)
{
    if (!s_setting_volume_editing) {
        return;
    }

    if (fabsf(roll) <= SETTING_TILT_NEUTRAL_DEG) {
        s_setting_tilt_state = 0;
        return;
    }

    if (roll >= SETTING_TILT_TRIGGER_DEG) {
        if (s_setting_tilt_state != 1) {
            s_setting_preview_volume = (s_setting_preview_volume > (100 - SETTING_VOLUME_STEP))
                                           ? 100
                                           : (uint8_t)(s_setting_preview_volume + SETTING_VOLUME_STEP);
            setting_ui_apply_preview_volume();
            s_setting_tilt_state = 1;
            last_action_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        }
        return;
    }

    if (roll <= -SETTING_TILT_TRIGGER_DEG) {
        if (s_setting_tilt_state != -1) {
            s_setting_preview_volume = (s_setting_preview_volume < SETTING_VOLUME_STEP)
                                           ? 0
                                           : (uint8_t)(s_setting_preview_volume - SETTING_VOLUME_STEP);
            setting_ui_apply_preview_volume();
            s_setting_tilt_state = -1;
            last_action_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        }
    }
}

void setting_ui_set_wifi_reset_armed(bool armed)
{
    setting_wifi_reset_armed = armed;
}

bool setting_ui_is_wifi_reset_armed(void)
{
    return setting_wifi_reset_armed;
}

void setting_ui_toggle_wifi_reset_armed(void)
{
    setting_wifi_reset_armed = !setting_wifi_reset_armed;
}

static bool s_radio_volume_editing = false;
static uint8_t s_radio_preview_volume = SETTINGS_DEFAULT_VOLUME;
static int8_t s_radio_tilt_state = 0;

#define RADIO_VOLUME_STEP 1
#define RADIO_TILT_TRIGGER_DEG 10.0f
#define RADIO_TILT_NEUTRAL_DEG 4.0f

static void radio_ui_apply_preview_volume(void)
{
    audio_player_set_volume(s_radio_preview_volume);
}

bool radio_ui_is_volume_editing(void)
{
    return s_radio_volume_editing;
}

void radio_ui_exit_volume_edit(bool save)
{
    if (!s_radio_volume_editing) {
        return;
    }

    if (save) {
        settings_set_volume(s_radio_preview_volume);
        settings_save_volume();
        audio_player_set_volume(s_radio_preview_volume);
    } else {
        uint8_t saved_volume = settings_get_volume();
        s_radio_preview_volume = saved_volume;
        audio_player_set_volume(saved_volume);
    }

    s_radio_volume_editing = false;
    s_radio_tilt_state = 0;
}

void radio_ui_toggle_volume_edit(void)
{
    if (s_radio_volume_editing) {
        radio_ui_exit_volume_edit(true);
        return;
    }

    s_radio_preview_volume = audio_player_get_volume();
    s_radio_volume_editing = true;
    s_radio_tilt_state = 0;
    radio_ui_apply_preview_volume();
}

void radio_ui_update_volume_tilt(float roll)
{
    if (!s_radio_volume_editing) {
        return;
    }

    if (fabsf(roll) <= RADIO_TILT_NEUTRAL_DEG) {
        s_radio_tilt_state = 0;
        return;
    }

    if (roll >= RADIO_TILT_TRIGGER_DEG) {
        if (s_radio_tilt_state != 1) {
            if (s_radio_preview_volume <= (100 - RADIO_VOLUME_STEP)) {
                s_radio_preview_volume = (uint8_t)(s_radio_preview_volume + RADIO_VOLUME_STEP);
            } else {
                s_radio_preview_volume = 100;
            }
            radio_ui_apply_preview_volume();
            s_radio_tilt_state = 1;
            last_action_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        }
        return;
    }

    if (roll <= -RADIO_TILT_TRIGGER_DEG) {
        if (s_radio_tilt_state != -1) {
            if (s_radio_preview_volume >= RADIO_VOLUME_STEP) {
                s_radio_preview_volume = (uint8_t)(s_radio_preview_volume - RADIO_VOLUME_STEP);
            } else {
                s_radio_preview_volume = 0;
            }
            radio_ui_apply_preview_volume();
            s_radio_tilt_state = -1;
            last_action_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        }
    }
}

// ui.c
static uint32_t s_sync_ui_start_ms = 0;

void reset_sync_ui_timer(void)
{
    s_sync_ui_start_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
}

void draw_syncing_ui(u8g2_t *u8g2)
{
    static const char *tips[] = {
        "Never Give Up",
        "A New Beginning",
        "Code Changes World",
        "Keep On Loving"
    };

    // 修复说明：改为使用 FreeRTOS 相对滴答时间，休眠期间不会累加。
    uint32_t ms_now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (s_sync_ui_start_ms == 0) {
        s_sync_ui_start_ms = ms_now;
    }
    uint32_t elapsed_ms = ms_now - s_sync_ui_start_ms;

    u8g2_ClearBuffer(u8g2);

    if (ap_wifi_is_config_mode_active()) {
        char buf[64];
        int16_t str_width;
        const uint8_t screen_width = 128;

        // --- 1. 标题栏 (高度压缩至 13px) ---
        u8g2_SetFont(u8g2, u8g2_font_wqy12_t_gb2312);
        u8g2_DrawBox(u8g2, 0, 0, 128, 13);           // 矩形高度减小
        u8g2_SetDrawColor(u8g2, 0); 
        const char* title = "Wi-Fi 配置";
        str_width = u8g2_GetUTF8Width(u8g2, title);
        u8g2_DrawUTF8(u8g2, (screen_width - str_width) / 2, 11, title); // 基线移至 11
        u8g2_SetDrawColor(u8g2, 1); 

        // --- 2. 信息展示区 (使用 12px 字体替代 14px 以节省空间) ---
        // 如果 7x14 导致溢出，建议这里也统一用 wqy12
        u8g2_SetFont(u8g2, u8g2_font_wqy12_t_gb2312); 

        // SSID 渲染 (位置上移)
        snprintf(buf, sizeof(buf), "ID: %s", wifi_manager_get_ap_ssid());
        str_width = u8g2_GetUTF8Width(u8g2, buf);
        u8g2_DrawUTF8(u8g2, (screen_width - str_width) / 2, 28, buf); 

        // Password 渲染 (紧贴 SSID)
        snprintf(buf, sizeof(buf), "PW: %s", wifi_manager_get_ap_password());
        str_width = u8g2_GetUTF8Width(u8g2, buf);
        u8g2_DrawUTF8(u8g2, (screen_width - str_width) / 2, 42, buf); 

        // --- 3. 底部修饰与提示 (严格控制在 64 像素内) ---
        u8g2_DrawHLine(u8g2, 24, 46, 80);            // 分隔线位置调至 46
        
        // 提示语：确保基线在 58-60，给汉字底部留出 4 像素空间
        const char* hint = "请设备连接热点";          // 缩短字数减少宽度压力
        str_width = u8g2_GetUTF8Width(u8g2, hint);
        u8g2_DrawUTF8(u8g2, (screen_width - str_width) / 2, 60, hint); 

        u8g2_SendBuffer(u8g2);
        return;
    }

    // 顶部状态栏
    u8g2_SetFont(u8g2, u8g2_font_6x12_tf); 
    u8g2_DrawStr(u8g2, 12, 12, "Syncing Time"); 

    int dot_idx = (elapsed_ms / 500) % 4; 
    for(int i = 0; i < dot_idx; i++) {
        u8g2_DrawStr(u8g2, 86 + (i * 4), 12, ".");
    }

    char time_buf[16];
    int seconds = elapsed_ms / 1000;
#if 0
    int seconds = ms_now / 1000; // 说明：原注释乱码，已修复为中文说明。
    #endif
    snprintf(time_buf, sizeof(time_buf), "%ds", seconds);
    int time_w = u8g2_GetStrWidth(u8g2, time_buf);
    u8g2_DrawStr(u8g2, 126 - time_w, 12, time_buf); 

    u8g2_DrawHLine(u8g2, 0, 16, 128); 

    // 中部：励志语
    int tip_idx = (elapsed_ms / 3000) % 4; 
    u8g2_SetFont(u8g2, u8g2_font_7x14_tf); 
    int str_width = u8g2_GetStrWidth(u8g2, tips[tip_idx]);
    u8g2_DrawStr(u8g2, (128 - str_width) / 2, 38, tips[tip_idx]); 

    u8g2_DrawHLine(u8g2, 0, 48, 128); 

    // 底部：小球动画
    int track_y = 58;
    int track_x_start = 14, track_x_end = 114;
    int track_len = track_x_end - track_x_start;
    u8g2_DrawHLine(u8g2, track_x_start, track_y, track_len);

    int cycle_ms = 2000;
    float t = (float)(elapsed_ms % cycle_ms) / cycle_ms; 
    int ball_x = (t <= 0.5f) ? (track_x_start + (int)(t * 2.0f * track_len)) : 
                               (track_x_end - (int)((t - 0.5f) * 2.0f * track_len));
    u8g2_DrawDisc(u8g2, ball_x, track_y, 3, U8G2_DRAW_ALL);

    u8g2_SendBuffer(u8g2);
}

void draw_main_clock_ui(u8g2_t *u8g2)
{
    char buf[64];
    time_t now;
    struct tm t;

    time(&now);
    localtime_r(&now, &t);

    u8g2_ClearBuffer(u8g2);

    // 顶部状态栏
    const char *days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    // 用 snprintf 替代 sprintf，避免溢出风险。
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %s %s", t.tm_year+1900, t.tm_mon+1, t.tm_mday, days[t.tm_wday], weather_data.weather);
    u8g2_DrawStr(u8g2, 1, 9, buf);

    u8g2_DrawHLine(u8g2, 0, 11, 128);
    u8g2_DrawHLine(u8g2, 0, 13, 128);

    // 时间：时分秒
    u8g2_SetFont(u8g2, u8g2_font_logisoso16_tn);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    u8g2_DrawStr(u8g2, 27, 34, buf);

    // 中部状态分隔线
    u8g2_DrawHLine(u8g2, 0, 37, 128);

    // 传感器信息区
    u8g2_SetFont(u8g2, u8g2_font_5x7_tf);

    // 列1：外温（OUT_T）
    u8g2_DrawStr(u8g2, 2, 45, "OUT_T"); 
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    snprintf(buf, sizeof(buf), "%dC", weather_data.temp_now); 
    u8g2_DrawStr(u8g2, 2, 57, buf);

    // 列2：海拔（X=45）
    u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
    u8g2_DrawStr(u8g2, 45, 45, "ALT"); 
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    snprintf(buf, sizeof(buf), "%.0fm", bmp280.altitude);
    u8g2_DrawStr(u8g2, 45, 57, buf); // 瀵归綈 X=45

    // 列3：内温（IN_T）
    u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
    u8g2_DrawStr(u8g2, 88, 45, "IN_T"); // 对齐 X=88
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    snprintf(buf, sizeof(buf), "%.1fC", bmp280.temperature); 
    u8g2_DrawStr(u8g2, 88, 57, buf); // 瀵归綈 X=88

    // 底部双分隔线（静态+动态）
    // 1. 上方静态线
    u8g2_DrawHLine(u8g2, 0, 60, 128);
    // 2. 下方动态线（15 秒循环增长）
    uint32_t ms_now = esp_timer_get_time() / 1000;
    int progress_width = (ms_now % 15000) * 128 / 15000; 
    u8g2_DrawHLine(u8g2, 0, 62, progress_width);

    u8g2_SendBuffer(u8g2);
}

// 建议将该结构体定义放在函数外，避免重复初始化。

int menu_layer = 1; // 默认在一级菜单
void draw_select_ui(u8g2_t *u8g2, ui_mode_e selected)
{
    static const game_info_t info_db[] = {
        [MODE_CLOCK]       = {"Clock",    123}, 
        [MODE_RADIO]       = {"Radio",    150},
        [MODE_GAME_SELECT] = {"Game",     207}, 
        [MODE_BLOOD]       = {"SpO2",     238},  
        [MODE_SETTING]     = {"System",   129},
        [MODE_RECORDER]    = {"Record",   137},
        [MODE_BALL]        = {"Ball",     175},  
        [MODE_DINO]        = {"Dino",     259}, 
        [MODE_PLANE]       = {"Plane",    165}, 
    };

    const ui_mode_e *active_list;
    int count = 0;

    // 直接根据菜单层级选择列表，避免猜测逻辑。
    if (menu_layer == 2) {
        active_list = sub_game_list;
        count = sizeof(sub_game_list) / sizeof(sub_game_list[0]);
    } else {
        active_list = main_app_list;
        count = sizeof(main_app_list) / sizeof(main_app_list[0]);
    }
    
    int cur = 0;
    for (int i = 0; i < count; i++) {
        if (active_list[i] == selected) { cur = i; break; }
    }
    int left_i  = (cur - 1 + count) % count;
    int right_i = (cur + 1) % count;

    u8g2_ClearBuffer(u8g2);

    // 顶部状态栏
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    if (menu_layer == 2) {
        u8g2_DrawStr(u8g2, 31, 10, "Select Game");
    } else {
        u8g2_DrawStr(u8g2, 34, 10, "Select App");
    }
    u8g2_DrawHLine(u8g2, 0, 12, 128);

    u8g2_DrawTriangle(u8g2, 4, 32, 8, 28, 8, 36);   
    u8g2_DrawTriangle(u8g2, 119, 28, 119, 36, 123, 32); 

    const char *left_name  = info_db[active_list[left_i]].name;
    const char *right_name = info_db[active_list[right_i]].name;

    // 仅在二级菜单中将游戏入口显示为 Back。
    if (menu_layer == 2 && active_list[left_i] == MODE_GAME_SELECT)  left_name = "Back";
    if (menu_layer == 2 && active_list[right_i] == MODE_GAME_SELECT) right_name = "Back";

    u8g2_SetFont(u8g2, u8g2_font_5x7_tr); 
    u8g2_DrawStr(u8g2, 12, 35, left_name);
    int right_name_w = u8g2_GetStrWidth(u8g2, right_name);
    u8g2_DrawStr(u8g2, 116 - right_name_w, 35, right_name);

    int center_x = 44, center_y = 15, w = 40, h = 34;
    u8g2_DrawRFrame(u8g2, center_x, center_y, w, h, 4);     
    
    u8g2_SetFont(u8g2, u8g2_font_open_iconic_all_2x_t); 

    const char *cur_name = info_db[selected].name;
    uint16_t cur_icon    = info_db[selected].icon_code;

    // 仅在二级菜单中修改名称和图标。
    if (menu_layer == 2 && selected == MODE_GAME_SELECT) {
        cur_name = "Back";
        cur_icon = 66; // 返回箭头
    }

    u8g2_DrawGlyph(u8g2, center_x + 12, center_y + 24, cur_icon);

    u8g2_SetFont(u8g2, u8g2_font_6x10_tr);
    int text_width = u8g2_GetStrWidth(u8g2, cur_name);
    int text_x = center_x + (w - text_width) / 2; 
    
    u8g2_DrawBox(u8g2, text_x - 2, center_y + h + 3 , text_width + 4, 11); 
    u8g2_SetDrawColor(u8g2, 0); 
    u8g2_DrawStr(u8g2, text_x, center_y + h + 11, cur_name);
    u8g2_SetDrawColor(u8g2, 1); 

    u8g2_SendBuffer(u8g2);
}

void draw_radio_ui(u8g2_t *u8g2)
{
    const audio_station_t *station = audio_player_get_station();
    audio_state_t state = audio_player_get_state();
    uint8_t volume = audio_player_get_volume();
    uint32_t ms_now = (uint32_t)(esp_timer_get_time() / 1000);
    bool vol_edit = radio_ui_is_volume_editing();
    char vol_buf[20];
    const char *hint_text = NULL;

    u8g2_ClearBuffer(u8g2);
    u8g2_SetFont(u8g2, u8g2_font_wqy12_t_gb2312);

    u8g2_DrawUTF8(u8g2, 2, 11, "网络电台");
    const char *state_label = "已停止";
    if (state == AUDIO_STATE_PLAYING) {
        state_label = "播放中";
    } else if (state == AUDIO_STATE_BUFFERING) {
        state_label = "缓冲中";
    } else if (state == AUDIO_STATE_ERROR) {
        state_label = "错误";
    }
    if (vol_edit) {
        state_label = "调音中";
    }
    int st_w = u8g2_GetUTF8Width(u8g2, state_label);
    u8g2_DrawUTF8(u8g2, 126 - st_w, 11, state_label);
    u8g2_DrawHLine(u8g2, 0, 13, 128);

    const char *wifi_label = wifi_manager_is_connect() ? "Wi-Fi: 已连接" : "Wi-Fi: 未连接";
    u8g2_DrawUTF8(u8g2, 2, 25, wifi_label);
    if (vol_edit) {
        snprintf(vol_buf, sizeof(vol_buf), "音量调节:%u%%", volume);
    } else {
        snprintf(vol_buf, sizeof(vol_buf), "音量:%u%%", volume);
    }
    int vol_w = u8g2_GetUTF8Width(u8g2, vol_buf);
    u8g2_DrawUTF8(u8g2, 126 - vol_w, 25, vol_buf);

    int box_y = 29;
    int box_h = 18;
    u8g2_DrawRFrame(u8g2, 0, box_y, 128, box_h, 2);

    u8g2_SetClipWindow(u8g2, 2, box_y + 1, 126, box_y + box_h - 1);
    int text_w = u8g2_GetUTF8Width(u8g2, station->name);
    int scroll_x;
    int text_y = box_y + 14;
    if (text_w > 120) {
        scroll_x = 4 - ((ms_now / 100) % (text_w + 40));
    } else {
        scroll_x = (128 - text_w) / 2;
    }
    u8g2_DrawUTF8(u8g2, scroll_x, text_y, station->name);
    u8g2_SetMaxClipWindow(u8g2);

    u8g2_DrawHLine(u8g2, 0, 49, 128);
    u8g2_SetFont(u8g2, u8g2_font_wqy12_t_gb2312);
    if (vol_edit) {
        static const char *vol_hints[] = {
            "调音模式",
            "左减右加",
            "长按保存",
            "超长退出"
        };
        hint_text = vol_hints[(ms_now / 1500) % (sizeof(vol_hints) / sizeof(vol_hints[0]))];
    } else {
        static const char *radio_hints[] = {
            "短按切台",
            "长按调音",
            "超长退出",
            "音量页左右倾斜"
        };
        hint_text = radio_hints[(ms_now / 1500) % (sizeof(radio_hints) / sizeof(radio_hints[0]))];
    }
    int hint_w = u8g2_GetUTF8Width(u8g2, hint_text);
    u8g2_DrawUTF8(u8g2, (128 - hint_w) / 2, 62, hint_text);

    u8g2_SendBuffer(u8g2);
}

/**
 * @brief 绘制悬浮球游戏界面
 * @param u8g2 屏幕句柄指针
 */
// 辅助函数：判断点 (px, py) 是否在矩形障碍物内
static bool is_point_in_box(float px, float py, Obstacle_t box, float margin) {
    return (px + margin > box.x && px - margin < box.x + box.w &&
            py + margin > box.y && py - margin < box.y + box.h);
}

void draw_ball_game(u8g2_t *u8g2) 
{
    static float x = 20.0f, y = 20.0f; // 球位置
    static float vx = 0.0f, vy = 0.0f;
    static int score = 0;

    static Obstacle_t wall = {54, 22, 20, 20}; // 障碍物
    static Coin_t coin = {100, 32, 3, true};    // 閲戝竵

    const uint8_t radius = 4;

    // ==========================================
    // 1. 吃金币与随机刷新逻辑
    // ==========================================
    if (coin.active) {
        float dx = x - coin.x;
        float dy = y - coin.y;
        if ((dx * dx + dy * dy) < (radius + coin.r) * (radius + coin.r)) {
            score++;
            
            // 随机生成新的障碍物位置（避开当前小球）
            do {
                // esp_random() 返回 32 位无符号数，取余后限制在安全区域
                wall.x = (esp_random() % (128 - 30)) + 5; // 预留边界
                wall.y = (esp_random() % (64 - 30)) + 5;
                wall.w = (esp_random() % 15) + 15; // 随机宽度 15~30
                wall.h = (esp_random() % 15) + 15; // 随机高度 15~30
            } while (is_point_in_box(x, y, wall, radius + 10)); // 若刷到球附近则重新生成

            // 随机生成新金币位置（避开球和障碍物）
            do {
                coin.x = (esp_random() % (128 - 10)) + 5;
                coin.y = (esp_random() % (64 - 10)) + 5;
            } while (is_point_in_box(coin.x, coin.y, wall, radius + 5) || // 不能在墙内
                     ((coin.x - x) * (coin.x - x) + (coin.y - y) * (coin.y - y)) < 900); // 与球至少 30 像素

            coin.active = true;
        }
    }

    // ==========================================
    // 2. 物理运动计算
    // ==========================================
    vx = (vx - euler_angle.roll * 0.15f) * 0.92f; 
    vy = (vy + euler_angle.pitch * 0.15f) * 0.92f;
    
    float next_x = x + vx;
    float next_y = y + vy;

    // AABB 碰撞检测
    if (next_x + radius > wall.x && next_x - radius < wall.x + wall.w &&
        y + radius > wall.y && y - radius < wall.y + wall.h) {
        vx = -vx * 0.5f;     // 水平方向反弹
        next_x = x;      
    }
    if (x + radius > wall.x && x - radius < wall.x + wall.w &&
        next_y + radius > wall.y && next_y - radius < wall.y + wall.h) {
        vy = -vy * 0.5f;    // 垂直方向反弹
        next_y = y;
    }

    x = next_x;
    y = next_y;

    // 杈圭晫闄愬埗
    if (x < radius) { x = radius; vx = -vx * 0.5f; }
    if (x > 128 - radius) { x = 128 - radius; vx = -vx * 0.5f; }
    if (y < radius) { y = radius; vy = -vy * 0.5f; }
    if (y > 64 - radius) { y = 64 - radius; vy = -vy * 0.5f; }

    // ==========================================
    // 3. UI 娓叉煋
    // ==========================================
    u8g2_ClearBuffer(u8g2);
    u8g2_DrawFrame(u8g2, 0, 0, 128, 64); 
    
    u8g2_DrawBox(u8g2, wall.x, wall.y, wall.w, wall.h); // 闅忔満澶у皬鐨勫
    
    if (coin.active) {
        u8g2_DrawDisc(u8g2, coin.x, coin.y, coin.r, U8G2_DRAW_ALL); 
    }

    u8g2_DrawDisc(u8g2, (u8g2_uint_t)x, (u8g2_uint_t)y, radius, U8G2_DRAW_ALL); 

    char score_str[16];
    snprintf(score_str, sizeof(score_str), "Score:%d", score);
    u8g2_SetFont(u8g2, u8g2_font_4x6_tf);
    u8g2_DrawStr(u8g2, 2, 8, score_str);

    u8g2_SendBuffer(u8g2);
}

DinoGame_t dino_game = {
    .state = STATE_RUNNING,
    .score = 0,
    .high_score = 0,
    .dino = { .y = 39.0f, .vy = 0.0f, .is_jumping = false, .is_ducking = false },
    .obs = { .x = 128, .type = 0, .bird_y = 35, .speed = 4.0f } // 初始速度为 4.0
};

void dino_game_reset(DinoGame_t *game) 
{
    game->state = STATE_RUNNING;
    game->score = 0;
    
    // 初始化恐龙
    game->dino.y = 39.0f;
    game->dino.vy = 0.0f;
    game->dino.is_jumping = false;
    game->dino.is_ducking = false;
    
    // 初始化障碍物
    game->obs.x = 128;
    game->obs.type = 0;
    game->obs.speed = 4;
}

void draw_dino_game(u8g2_t *u8g2) 
{
    if (in_select) return; // 菜单切换时跳过游戏绘制

// ==========================================
    // A. 游戏结束界面（STATE_GAMEOVER）
    // ==========================================
    if (dino_game.state == STATE_GAMEOVER) {
        u8g2_ClearBuffer(u8g2);
        int w; // 用于暂存字符串像素宽度

        // 1. 绘制 "GAME OVER"（6x12 字体）
        u8g2_SetFont(u8g2, u8g2_font_6x12_tr);
        w = u8g2_GetStrWidth(u8g2, "GAME OVER");
        u8g2_DrawStr(u8g2, (128 - w) / 2, 22, "GAME OVER");
        
        // 2. 绘制当前分数（6x10 字体）
        char score_buf[20];
        snprintf(score_buf, sizeof(score_buf), "Score: %d", (int)dino_game.score);
        u8g2_SetFont(u8g2, u8g2_font_6x10_tr);
        w = u8g2_GetStrWidth(u8g2, score_buf);
        u8g2_DrawStr(u8g2, (128 - w) / 2, 36, score_buf);

        // 3. 绘制按键提示（6x10 字体）
        u8g2_SetFont(u8g2, u8g2_font_6x10_tr);
        
        w = u8g2_GetStrWidth(u8g2, "SHORT KEY: RETRY");
        u8g2_DrawStr(u8g2, (128 - w) / 2, 50, "SHORT KEY: RETRY");

        w = u8g2_GetStrWidth(u8g2, "LONG KEY: MENU");
        u8g2_DrawStr(u8g2, (128 - w) / 2, 62, "LONG KEY: MENU");
        
        u8g2_SendBuffer(u8g2);
        return;
    }

    // ==========================================
    // B. 游戏运行逻辑（STATE_RUNNING）
    // ==========================================

    // 1. MPU6050 姿态控制输入
    if (!dino_game.dino.is_jumping && !dino_game.dino.is_ducking && euler_angle.pitch > 25.0f) {
        dino_game.dino.is_jumping = true; 
        dino_game.dino.vy = -5.5f; // 起跳初速度
    }
    dino_game.dino.is_ducking = (euler_angle.pitch < -20.0f);

    // 2. 恐龙物理更新（跳跃与下落）
    if (dino_game.dino.is_jumping) {
        dino_game.dino.vy += 0.65f; // 重力加速度
        dino_game.dino.y += dino_game.dino.vy;
        if (dino_game.dino.y >= 39.0f) { // 落地检测
            dino_game.dino.y = 39.0f;
            dino_game.dino.vy = 0.0f;
            dino_game.dino.is_jumping = false;
        }
    }

    // 3. 分数增长与速度动态调整
    dino_game.score += 0.2f; // 基于生存时间累计分数
    
    // 基础速度 4.0，按分数逐步增加，并限制上限 9.0。
    dino_game.obs.speed = 4.0f + (dino_game.score / 200.0f);
    if (dino_game.obs.speed > 9.0f) {
        dino_game.obs.speed = 9.0f; 
    }

    // 4. 障碍物向左移动
    dino_game.obs.x -= (int)dino_game.obs.speed; 
    if (dino_game.obs.x < -20) {
        dino_game.obs.x = 128; // 移出屏幕后重置到右侧
        dino_game.obs.type = esp_random() % 2; // 随机：0=仙人掌，1=飞鸟
        dino_game.obs.bird_y = (esp_random() % 15) + 30; // 飞鸟高度 30~45
    }

    // ==========================================
    // C. 核心碰撞检测（AABB）
    // ==========================================
    
    // 恐龙 Hitbox
    int d_x = 15;
    int d_y = (int)dino_game.dino.y;
    int d_w = 12;
    int d_h = (dino_game.dino.is_ducking ? 8 : 14); // 蹲下时高度更低
    if (dino_game.dino.is_ducking) d_y += 6; // 蹲下时碰撞框下移

    // 障碍物 Hitbox
    int o_x = dino_game.obs.x;
    int o_y, o_w, o_h;
    if (dino_game.obs.type == 0) { // 仙人掌
        o_y = 40; o_w = 8; o_h = 15;
    } else { // 飞鸟
        o_y = dino_game.obs.bird_y; o_w = 12; o_h = 6;
    }

    // 经典 AABB 碰撞判断
    if (d_x < o_x + o_w && d_x + d_w > o_x &&
        d_y < o_y + o_h && d_y + d_h > o_y) {
        
        dino_game.state = STATE_GAMEOVER; // 碰撞后切换到结束状态
        if ((int)dino_game.score > dino_game.high_score) {
            dino_game.high_score = (int)dino_game.score;
        }
    }

    // ==========================================
    // D. OLED 画面渲染
    // ==========================================
    u8g2_ClearBuffer(u8g2);

    // 1. 画地面
    u8g2_DrawHLine(u8g2, 0, 55, 128);

    // 2. 画恐龙
    if (dino_game.dino.is_ducking && !dino_game.dino.is_jumping) {
        u8g2_DrawBox(u8g2, d_x, d_y, 18, 5); // 蹲下形态
    } else {
        u8g2_DrawBox(u8g2, d_x + 4, d_y, 10, 6); // 头
        u8g2_DrawBox(u8g2, d_x, d_y + 4, 12, 8); // 身体
        
        // 跑步腿部动画
        if (!dino_game.dino.is_jumping && (dino_game.obs.x % 10 < 5)) {
            u8g2_DrawVLine(u8g2, d_x + 3, d_y + 12, 4);
        } else if (!dino_game.dino.is_jumping) {
            u8g2_DrawVLine(u8g2, d_x + 8, d_y + 12, 4);
        } else {
            u8g2_DrawVLine(u8g2, d_x + 3, d_y + 12, 3); // 跳跃时收腿
            u8g2_DrawVLine(u8g2, d_x + 8, d_y + 12, 3);
        }
    }

    // 3. 画障碍物
    if (dino_game.obs.type == 0) { // 仙人掌
        u8g2_DrawBox(u8g2, dino_game.obs.x + 2, 40, 4, 15);
        u8g2_DrawBox(u8g2, dino_game.obs.x, 44, 2, 6);
        u8g2_DrawBox(u8g2, dino_game.obs.x + 6, 42, 2, 6);
    } else { // 飞鸟
        u8g2_DrawBox(u8g2, dino_game.obs.x, dino_game.obs.bird_y, 12, 4); // 鸟身
        if (dino_game.obs.x % 12 < 6) { // 扇动翅膀动画
            u8g2_DrawHLine(u8g2, dino_game.obs.x + 3, dino_game.obs.bird_y - 2, 6); // 翅膀向上
        } else {
            u8g2_DrawHLine(u8g2, dino_game.obs.x + 3, dino_game.obs.bird_y + 5, 6); // 翅膀向下
        }
    }

    // 4. 右上角计分栏
    char buf[20];
    snprintf(buf, sizeof(buf), "HI %04d  %04d", dino_game.high_score, (int)dino_game.score);
    u8g2_SetFont(u8g2, u8g2_font_4x6_tf);
    u8g2_DrawStr(u8g2, 75, 8, buf);

    u8g2_SendBuffer(u8g2);
}

// 初始化全局游戏实例
AirGame_t air_game = {
    .state = STATE_RUNNING,
    .score = 0,
    .player = { .x = 64, .y = 50, .vx = 0, .vy = 0, .width = 11, .height = 9 },
    
    // 数组中的每个元素都是一个结构体
    .bullets = { {0}, {0}, {0}, {0}, {0} }, 
    .enemies = { {0}, {0}, {0} }
};

void air_game_reset(AirGame_t *game) 
{
    // 1. 清零整个结构体（包含子弹和敌机数组）
    memset(game, 0, sizeof(AirGame_t));

    // 2. 设置游戏运行状态
    game->state = STATE_RUNNING;
    game->score = 0;

    // 3. 设置玩家初始数据
    game->player.x = 64;
    game->player.y = 50;
    game->player.width = 11;
    game->player.height = 9;
}

void draw_plane_game(u8g2_t *u8g2)
{
    if (in_select) return;

    // ==========================================
    // A. 游戏结束界面（STATE_GAMEOVER）
    // ==========================================
    if (air_game.state == STATE_GAMEOVER) {
        u8g2_ClearBuffer(u8g2);
        int w; // 用于临时保存字符串像素宽度

        // 1. 绘制 "GAME OVER"（6x12 字体）
        u8g2_SetFont(u8g2, u8g2_font_6x12_tr);
        w = u8g2_GetStrWidth(u8g2, "GAME OVER");
        u8g2_DrawStr(u8g2, (128 - w) / 2, 22, "GAME OVER"); // Y 坐标略微上移
        
        // 2. 绘制分数
        char buf[20];
        snprintf(buf, sizeof(buf), "SCORE: %d", air_game.score);

        w = u8g2_GetStrWidth(u8g2, buf);
        u8g2_DrawStr(u8g2, (128 - w) / 2, 36, buf);

        u8g2_SetFont(u8g2, u8g2_font_6x10_tr);
        w = u8g2_GetStrWidth(u8g2, "SHORT KEY: RESTART");
        u8g2_DrawStr(u8g2, (128 - w) / 2, 50, "SHORT KEY: RESTART");

        u8g2_SetFont(u8g2, u8g2_font_6x10_tr);
        w = u8g2_GetStrWidth(u8g2, "LONG KEY: MENU");
        u8g2_DrawStr(u8g2, (128 - w) / 2, 62, "LONG KEY: MENU");
        
        u8g2_SendBuffer(u8g2);
        return;
    }

    // ==========================================
    // B. MPU6050 战机物理控制
    // ==========================================
    
    // 优化战机移动手感：X 轴偏航，Y 轴俯仰。
    air_game.player.vx = (air_game.player.vx - euler_angle.roll * 0.18f) * 0.94f;
    air_game.player.vy = (air_game.player.vy + euler_angle.pitch * 0.18f) * 0.94f;

    // 小角度抖动时衰减速度，避免飞机持续漂移。
    if (fabs(euler_angle.roll) < 1.5f) air_game.player.vx *= 0.8f; 
    if (fabs(euler_angle.pitch) < 1.5f) air_game.player.vy *= 0.8f;

    air_game.player.x += air_game.player.vx;
    air_game.player.y += air_game.player.vy;

    // 屏幕边界限制
    if (air_game.player.x < 5) air_game.player.x = 5;
    if (air_game.player.x > 123) air_game.player.x = 123;
    if (air_game.player.y < 5) air_game.player.y = 5;
    if (air_game.player.y > 59) air_game.player.y = 59;

    // ==========================================
    // C. 游戏内部逻辑（子弹、敌机）
    // ==========================================

    // 1. 自动发射子弹（基于帧计数）
    static int fire_tick = 0;
    fire_tick++;
    if (fire_tick >= 2) { // 每 2 帧发射一发
        fire_tick = 0;
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!air_game.bullets[i].active) {
                air_game.bullets[i].x = (int)air_game.player.x;
                air_game.bullets[i].y = (int)air_game.player.y - 5;
                air_game.bullets[i].active = true;
                break;
            }
        }
    }

    // 子弹向上移动
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (air_game.bullets[i].active) {
            air_game.bullets[i].y -= 3;
            if (air_game.bullets[i].y < 0) air_game.bullets[i].active = false;
        }
    }

    // 2. 敌机下落逻辑
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!air_game.enemies[i].active) { // 刷新敌机
            air_game.enemies[i].x = (esp_random() % 110) + 10;
            air_game.enemies[i].y = -(esp_random() % 30);
            air_game.enemies[i].speed = (esp_random() % 2) + 1;
            air_game.enemies[i].active = true;
        } else {
            air_game.enemies[i].y += air_game.enemies[i].speed;
            if (air_game.enemies[i].y > 64) air_game.enemies[i].active = false; // 飞出底边后失效
        }
    }

    // 3. 子弹与敌机碰撞（Hitbox: 敌机 8x8，子弹 1x3）
    for (int b = 0; b < MAX_BULLETS; b++) {
        if (!air_game.bullets[b].active) continue;
        for (int e = 0; e < MAX_ENEMIES; e++) {
            if (!air_game.enemies[e].active) continue;

            if (air_game.bullets[b].x > air_game.enemies[e].x - 4 && air_game.bullets[b].x < air_game.enemies[e].x + 4 &&
                air_game.bullets[b].y > air_game.enemies[e].y - 4 && air_game.bullets[b].y < air_game.enemies[e].y + 4) {
                
                air_game.bullets[b].active = false;
                air_game.enemies[e].active = false; // 击毁
                air_game.score += 10;
            }
        }
    }

    // 4. 敌机与玩家战机碰撞（玩家 11x9，敌机 8x8）
    for (int e = 0; e < MAX_ENEMIES; e++) {
        if (!air_game.enemies[e].active) continue;
        if (air_game.player.x - 5 < air_game.enemies[e].x + 4 && air_game.player.x + 5 > air_game.enemies[e].x - 4 &&
            air_game.player.y - 4 < air_game.enemies[e].y + 4 && air_game.player.y + 4 > air_game.enemies[e].y - 4) {
            
            air_game.state = STATE_GAMEOVER; // 坠毁
        }
    }

    // ==========================================
    // D. OLED UI 画布渲染
    // ==========================================
    u8g2_ClearBuffer(u8g2);

    // 1. 画星空背景
    static int star_y = 0;
    star_y = (star_y + 1) % 64;
    u8g2_DrawPixel(u8g2, 20, star_y);
    u8g2_DrawPixel(u8g2, 80, (star_y + 30) % 64);
    u8g2_DrawPixel(u8g2, 110, (star_y + 10) % 64);

    // 2. 绘制战机（三角形）
    int px = (int)air_game.player.x;
    int py = (int)air_game.player.y;
    u8g2_DrawTriangle(u8g2, px, py - 5, px - 5, py + 4, px + 5, py + 4); // 机身
    u8g2_DrawHLine(u8g2, px - 8, py + 2, 17); // 机翼

    // 3. 绘制敌机
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (air_game.enemies[i].active) {
            int ex = air_game.enemies[i].x;
            int ey = air_game.enemies[i].y;
            u8g2_DrawBox(u8g2, ex - 3, ey - 3, 7, 7); // 敌机核心
            u8g2_DrawHLine(u8g2, ex - 5, ey, 11);
        }
    }

    // 4. 绘制子弹
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (air_game.bullets[i].active) {
            u8g2_DrawVLine(u8g2, air_game.bullets[i].x, air_game.bullets[i].y, 3);
        }
    }

    // 5. 得分栏
    char score_str[16];
    snprintf(score_str, sizeof(score_str), "SCORE:%04d", air_game.score);
    u8g2_SetFont(u8g2, u8g2_font_4x6_tf);
    u8g2_DrawStr(u8g2, 2, 8, score_str);

    u8g2_SendBuffer(u8g2);
}

static uint32_t sample_start = 0; 

void reset_blood_ui_timer(void) {
    sample_start = 0; 
}

void draw_blood_ui(u8g2_t *u8g2)
{
    char buf[32]; //
    u8g2_ClearBuffer(u8g2); //

    // 顶部标题
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf); //
    u8g2_DrawStr(u8g2, 25, 9, "Health Monitor"); //
    u8g2_DrawHLine(u8g2, 0, 11, 128); //
    u8g2_DrawHLine(u8g2, 0, 13, 128); //

    switch (b_state) //
    {
        case BLOOD_IDLE: //
            // 提示放手指
            u8g2_SetFont(u8g2, u8g2_font_6x10_tf); //
            u8g2_DrawStr(u8g2, 10, 35, "Place finger on"); //
            u8g2_DrawStr(u8g2, 20, 47, "the sensor..."); //

            if ((xTaskGetTickCount() / 500) % 2 == 0) { //
                u8g2_DrawBox(u8g2, 58, 53, 6, 6); //
                u8g2_DrawBox(u8g2, 64, 53, 6, 6); //
                u8g2_DrawBox(u8g2, 55, 56, 18, 4); //
                u8g2_DrawBox(u8g2, 58, 60, 12, 3); //
                u8g2_DrawBox(u8g2, 61, 63, 6, 2); //
            }
            break; //

        case BLOOD_SAMPLING: //
        {
            u8g2_SetFont(u8g2, u8g2_font_6x10_tf); //
            u8g2_DrawStr(u8g2, 22, 30, "Measuring..."); //

            if (sample_start == 0) sample_start = xTaskGetTickCount(); //
            
            uint32_t elapsed = xTaskGetTickCount() - sample_start; //
            int bar = (elapsed / 51); //
            if (bar > 128) bar = 128; //

            u8g2_DrawFrame(u8g2, 0, 38, 128, 8); //
            u8g2_DrawBox(u8g2, 0, 38, bar, 8); //

            u8g2_SetFont(u8g2, u8g2_font_5x7_tf); //
            u8g2_DrawStr(u8g2, 2, 56, "Keep still..."); //

            break; //
        }

        case BLOOD_DONE: //
            // 显示心率
            u8g2_SetFont(u8g2, u8g2_font_5x7_tf); //
            u8g2_DrawStr(u8g2, 2, 26, "Heart Rate"); //
            u8g2_SetFont(u8g2, u8g2_font_logisoso16_tn); //
            snprintf(buf, sizeof(buf), "%3d", b_data.heart); //
            u8g2_DrawStr(u8g2, 2, 44, buf); //
            u8g2_SetFont(u8g2, u8g2_font_5x7_tf); //
            u8g2_DrawStr(u8g2, 52, 44, "bpm"); //

            u8g2_DrawVLine(u8g2, 76, 15, 40); //

            // 显示血氧
            u8g2_SetFont(u8g2, u8g2_font_5x7_tf); //
            u8g2_DrawStr(u8g2, 80, 26, "SpO2"); //
            u8g2_SetFont(u8g2, u8g2_font_logisoso16_tn); //
            snprintf(buf, sizeof(buf), "%3d", (int)b_data.SpO2); //
            u8g2_DrawStr(u8g2, 80, 44, buf); //
            u8g2_SetFont(u8g2, u8g2_font_5x7_tf); //
            u8g2_DrawStr(u8g2, 112, 44, "%"); //

            u8g2_DrawHLine(u8g2, 0, 48, 128); //
            u8g2_SetFont(u8g2, u8g2_font_5x7_tf); //
            if (b_data.SpO2 >= 85 && b_data.heart >= 60 && b_data.heart <= 110) { //
                u8g2_DrawStr(u8g2, 20, 58, "Status: Normal"); //
            } else { //
                u8g2_DrawStr(u8g2, 14, 58, "Status: Abnormal!"); //
            }
            break; //
    }

    u8g2_SendBuffer(u8g2); 
}

void draw_recorder_ui(u8g2_t *u8g2)
{
    recorder_state_t state = recorder_get_state();
    uint16_t peak = recorder_get_peak_level();
    uint32_t recorded_ms = recorder_get_recorded_ms();
    int bar_w = (peak * 120) / 32767;
    char buf[24];
    const char *state_text = "Standby";
    const char *hint_text = "Short: Rec  Long: Menu";

    if (bar_w < 0) {
        bar_w = 0;
    }
    if (bar_w > 120) {
        bar_w = 120;
    }

    if (state == RECORDER_STATE_RECORDING) {
        state_text = "Recording";
        hint_text = "Short: Stop && Play";
    } else if (state == RECORDER_STATE_PLAYING) {
        state_text = recorder_get_play_variant_name();
        hint_text = "Short: Stop";
    } else if (state == RECORDER_STATE_ERROR) {
        state_text = "Error";
        hint_text = "Check mic / I2S";
    }

    u8g2_ClearBuffer(u8g2);
    u8g2_SetFont(u8g2, u8g2_font_6x12_tf);
    u8g2_DrawStr(u8g2, 22, 10, "- RECORDER -");
    u8g2_DrawHLine(u8g2, 0, 12, 128);

    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    snprintf(buf, sizeof(buf), "State: %s", state_text);
    u8g2_DrawStr(u8g2, 4, 25, buf);
    snprintf(buf, sizeof(buf), "Time : %lus", (unsigned long)(recorded_ms / 1000U));
    u8g2_DrawStr(u8g2, 4, 37, buf);

    u8g2_DrawFrame(u8g2, 4, 42, 120, 10);
    if (bar_w > 0) {
        u8g2_DrawBox(u8g2, 4, 42, bar_w, 10);
    }

    u8g2_DrawStr(u8g2, 10, 61, hint_text);
    u8g2_SendBuffer(u8g2);
}

void draw_setting_ui(u8g2_t *u8g2)
{
    char buf[24];
    uint32_t ms_now = (uint32_t)(esp_timer_get_time() / 1000);
    static const char *motto[] = {
        "Keep On Loving",
        "World Peace",
        "Code Changes World",
        "Power on, Game on!",
        "Hello World!",
        "Overclock your life",
        "Make debug, not war",
        "Peace & Love",
        "No reset, no regret"
    };

    u8g2_ClearBuffer(u8g2);

    if (s_setting_page == SETTING_PAGE_INFO) {
        const char *github_link = "github.com/abcuer";
        int motto_idx = (ms_now / 3000) % (sizeof(motto) / sizeof(motto[0]));
        int motto_w;
        int github_x_start = 56;
        int github_width = 128 - github_x_start - 2;
        int text_len;
        int scroll_offset;
        int cur_x;

        u8g2_SetFont(u8g2, u8g2_font_6x12_tf);
        u8g2_DrawStr(u8g2, 16, 10, "- SYSTEM INFO -");
        u8g2_DrawHLine(u8g2, 0, 12, 128);

        u8g2_SetFont(u8g2, u8g2_font_open_iconic_all_1x_t);
        u8g2_DrawGlyph(u8g2, 2, 26, 64 + 19);
        u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
        u8g2_DrawStr(u8g2, 14, 26, "Bili  : FASQwQ");

        u8g2_SetFont(u8g2, u8g2_font_open_iconic_all_1x_t);
        u8g2_DrawGlyph(u8g2, 2, 40, 64 + 4);
        u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
        u8g2_DrawStr(u8g2, 14, 40, "Github:");

        u8g2_SetMaxClipWindow(u8g2);
        u8g2_SetClipWindow(u8g2, github_x_start, 30, 126, 42);
        text_len = u8g2_GetStrWidth(u8g2, github_link);
        scroll_offset = (ms_now / 30) % (text_len + github_width);
        cur_x = (github_x_start + github_width) - scroll_offset;
        u8g2_DrawStr(u8g2, cur_x, 40, github_link);
        u8g2_SetMaxClipWindow(u8g2);

        u8g2_DrawHLine(u8g2, 0, 44, 128);
        u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
        motto_w = u8g2_GetStrWidth(u8g2, motto[motto_idx]);
        u8g2_DrawStr(u8g2, (128 - motto_w) / 2, 59, motto[motto_idx]);
        u8g2_SendBuffer(u8g2);
        return;
    }

    if (s_setting_page == SETTING_PAGE_VOLUME) {
        char buf[16];
        int16_t str_width;
        const uint8_t screen_width = 128;
        u8g2_SetFont(u8g2, u8g2_font_wqy12_t_gb2312);
        u8g2_DrawUTF8(u8g2, 40, 11, "音量调节");
        u8g2_DrawHLine(u8g2, 0, 13, 128); // 稍微加粗分割感

        snprintf(buf, sizeof(buf), "%u%%", s_setting_preview_volume);
        u8g2_SetFont(u8g2, u8g2_font_logisoso16_tn);
    
        str_width = u8g2_GetStrWidth(u8g2, buf);
        int16_t val_x = (screen_width - str_width) / 2;
        u8g2_DrawStr(u8g2, val_x, 40, buf);

        u8g2_DrawTriangle(u8g2, 12, 32, 22, 26, 22, 38);
        // 右箭头：指向右 (113,32)
        u8g2_DrawTriangle(u8g2, 116, 32, 106, 26, 106, 38);

        u8g2_SetFont(u8g2, u8g2_font_wqy12_t_gb2312);

        const char* hint1 = "左减 右加 回正触发";
        str_width = u8g2_GetUTF8Width(u8g2, hint1);
        u8g2_DrawUTF8(u8g2, (screen_width - str_width) / 2, 53, hint1);
        const char* hint2 = "长按保存并返回";
        str_width = u8g2_GetUTF8Width(u8g2, hint2);
        u8g2_DrawUTF8(u8g2, (screen_width - str_width) / 2, 64, hint2);

        u8g2_SendBuffer(u8g2);
        return;
    }

    if (s_setting_page == SETTING_PAGE_WIFI_RESET) {
        u8g2_SetFont(u8g2, u8g2_font_wqy12_t_gb2312);
        u8g2_DrawUTF8(u8g2, 28, 11, "重置 Wi-Fi");
        u8g2_DrawHLine(u8g2, 0, 12, 128);
        u8g2_DrawUTF8(u8g2, 12, 28, "将清除已保存网络");
        u8g2_DrawUTF8(u8g2, 16, 42, "长按立即执行重置");
        u8g2_DrawUTF8(u8g2, 22, 63, "重启后重新配网");
        u8g2_SendBuffer(u8g2);
        return;
    }

    u8g2_SetFont(u8g2, u8g2_font_6x12_tf);
    u8g2_DrawStr(u8g2, 16, 10, "- SYSTEM SET -");
    u8g2_DrawHLine(u8g2, 0, 12, 128);

    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    if (s_setting_item == SETTING_ITEM_INFO) {
        u8g2_DrawBox(u8g2, 2, 16, 124, 11);
        u8g2_SetDrawColor(u8g2, 0);
        u8g2_DrawStr(u8g2, 6, 25, "System Info");
        u8g2_SetDrawColor(u8g2, 1);
    } else {
        u8g2_DrawFrame(u8g2, 2, 16, 124, 11);
        u8g2_DrawStr(u8g2, 6, 25, "System Info");
    }

    snprintf(buf, sizeof(buf), "Volume: %u%%", settings_get_volume());
    if (s_setting_item == SETTING_ITEM_VOLUME) {
        u8g2_DrawBox(u8g2, 2, 29, 124, 11);
        u8g2_SetDrawColor(u8g2, 0);
        u8g2_DrawStr(u8g2, 6, 38, buf);
        u8g2_SetDrawColor(u8g2, 1);
    } else {
        u8g2_DrawFrame(u8g2, 2, 29, 124, 11);
        u8g2_DrawStr(u8g2, 6, 38, buf);
    }

    if (s_setting_item == SETTING_ITEM_WIFI_RESET) {
        u8g2_DrawBox(u8g2, 2, 42, 124, 11);
        u8g2_SetDrawColor(u8g2, 0);
        u8g2_DrawStr(u8g2, 6, 51, "Reset WiFi");
        u8g2_SetDrawColor(u8g2, 1);
    } else {
        u8g2_DrawFrame(u8g2, 2, 42, 124, 11);
        u8g2_DrawStr(u8g2, 6, 51, "Reset WiFi");
    }

    if (s_setting_item == SETTING_ITEM_EXIT) {
        u8g2_DrawBox(u8g2, 2, 55, 124, 9);
        u8g2_SetDrawColor(u8g2, 0);
        u8g2_DrawStr(u8g2, 6, 63, "Exit");
        u8g2_SetDrawColor(u8g2, 1);
    } else {
        u8g2_DrawFrame(u8g2, 2, 55, 124, 9);
        u8g2_DrawStr(u8g2, 6, 63, "Exit");
    }

    u8g2_SendBuffer(u8g2);
}
