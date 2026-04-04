
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

#define SETTING_VOLUME_STEP          5
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

// ui.c
void draw_syncing_ui(u8g2_t *u8g2)
{
    static const char *tips[] = {
        "Never Give Up",
        "A New Beginning",
        "Code Changes World",
        "Keep On Loving"
    };

    // 馃幆 淇 1锛氭敼鐢?FreeRTOS 鐩稿婊寸瓟鏃堕棿銆傜潯瑙夋椂瀹冧細鏆傚仠锛岄啋鏉ユ墠缁х画鏁帮紒
    uint32_t ms_now = xTaskGetTickCount() * portTICK_PERIOD_MS; 

    u8g2_ClearBuffer(u8g2);

    // 鈹€鈹€ 椤堕儴鐘舵€佹爮 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    u8g2_SetFont(u8g2, u8g2_font_6x12_tf); 
    u8g2_DrawStr(u8g2, 12, 12, "Syncing Time"); 

    int dot_idx = (ms_now / 500) % 4; 
    for(int i = 0; i < dot_idx; i++) {
        u8g2_DrawStr(u8g2, 86 + (i * 4), 12, ".");
    }

    char time_buf[16];
    int seconds = ms_now / 1000; // 馃幆 姝ゆ椂鏄剧ず鐨勫崟娆￠厤缃戞椂闂村氨涓嶄細璺宠穬鍒板嚑鐧剧浜嗭紒
    snprintf(time_buf, sizeof(time_buf), "%ds", seconds);
    int time_w = u8g2_GetStrWidth(u8g2, time_buf);
    u8g2_DrawStr(u8g2, 126 - time_w, 12, time_buf); 

    u8g2_DrawHLine(u8g2, 0, 16, 128); 

    // 鈹€鈹€ 涓儴锛氬姳蹇楄 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    int tip_idx = (ms_now / 3000) % 4; 
    u8g2_SetFont(u8g2, u8g2_font_7x14_tf); 
    int str_width = u8g2_GetStrWidth(u8g2, tips[tip_idx]);
    u8g2_DrawStr(u8g2, (128 - str_width) / 2, 38, tips[tip_idx]); 

    u8g2_DrawHLine(u8g2, 0, 48, 128); 

    // 鈹€鈹€ 搴曢儴锛氬皬鐞冨姩鐢?鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    int track_y = 58;
    int track_x_start = 14, track_x_end = 114;
    int track_len = track_x_end - track_x_start;
    u8g2_DrawHLine(u8g2, track_x_start, track_y, track_len);

    int cycle_ms = 2000;
    float t = (float)(ms_now % cycle_ms) / cycle_ms; 
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

    // 鈹€鈹€ 椤堕儴鐘舵€佹爮 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    const char *days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    // 鎶?sprintf 鏀逛负 snprintf 闃叉孩鍑猴紝淇濇寔瀹夊叏涔犳儻
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %s %s", t.tm_year+1900, t.tm_mon+1, t.tm_mday, days[t.tm_wday], weather_data.weather);
    u8g2_DrawStr(u8g2, 1, 9, buf);

    u8g2_DrawHLine(u8g2, 0, 11, 128);
    u8g2_DrawHLine(u8g2, 0, 13, 128);

    // 鈹€鈹€ 鏃堕棿锛氭椂:鍒?绉?鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    u8g2_SetFont(u8g2, u8g2_font_logisoso16_tn);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    u8g2_DrawStr(u8g2, 27, 34, buf);

    // 鈹€鈹€ 涓儴鍒嗗壊绾匡紙涓婄Щ锛?3鈫?7锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    u8g2_DrawHLine(u8g2, 0, 37, 128);

    // 鈹€鈹€ 浼犳劅鍣ㄥ尯 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    u8g2_SetFont(u8g2, u8g2_font_5x7_tf);

    // 馃敘 鍒?: 澶栨俯 (绮剧畝瀛楃涓?OUT_T 閬垮厤鍜屼腑闂存墦鏋?
    u8g2_DrawStr(u8g2, 2, 45, "OUT_T"); 
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    snprintf(buf, sizeof(buf), "%dC", weather_data.temp_now); 
    u8g2_DrawStr(u8g2, 2, 57, buf);

    // 馃敘 鍒?: 娴锋嫈 (瀹岀編灞呬腑 X=45)
    u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
    u8g2_DrawStr(u8g2, 45, 45, "ALT"); 
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    snprintf(buf, sizeof(buf), "%.0fm", bmp280.altitude);
    u8g2_DrawStr(u8g2, 45, 57, buf); // 瀵归綈 X=45

    // 馃敘 鍒?: 鍐呮俯 (绮剧畝瀛楃涓?IN_T)
    u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
    u8g2_DrawStr(u8g2, 88, 45, "IN_T"); // 浠?X=88 寰€鍙虫覆鏌?
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    snprintf(buf, sizeof(buf), "%.1fC", bmp280.temperature); 
    u8g2_DrawStr(u8g2, 88, 57, buf); // 瀵归綈 X=88

    // 鈹€鈹€ 搴曢儴鍙屽垎鍓茬嚎锛堥潤鎬?鍔ㄦ€侊級 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    // 1. 杈冧笂鐨勯潤鎬佺嚎
    u8g2_DrawHLine(u8g2, 0, 60, 128);
    // 2. 杈冧笅鐨勫姩鎬佺嚎锛?5绉掍竴杞紝涓濇粦澧為暱锛?
    uint32_t ms_now = esp_timer_get_time() / 1000;
    int progress_width = (ms_now % 15000) * 128 / 15000; 
    u8g2_DrawHLine(u8g2, 0, 62, progress_width);

    u8g2_SendBuffer(u8g2);
}

// 寤鸿鎶婅繖涓粨鏋勪綋瀹氫箟鎷夊埌鍑芥暟澶栭潰锛岄槻姝㈡瘡娆¤皟鐢ㄥ嚱鏁伴兘閲嶆柊鍦ㄦ爤涓婂垵濮嬪寲

int menu_layer = 1; // 榛樿鍦?绾ц彍鍗?
void draw_select_ui(u8g2_t *u8g2, ui_mode_e selected)
{
    static const game_info_t info_db[] = {
        [MODE_CLOCK]       = {"Clock",    123}, 
        [MODE_BLOOD]       = {"SpO2",     238},  
        [MODE_GAME_SELECT] = {"Game",     207}, 
        [MODE_RADIO]       = {"Radio",    150},
        [MODE_SETTING]     = {"System",   129},
        [MODE_BALL]        = {"Ball",     175},  
        [MODE_DINO]        = {"Dino",     259}, 
        [MODE_PLANE]       = {"Plane",    165}, 
    };

    const ui_mode_e *active_list;
    int count = 0;

    // 馃幆 鏀惧純鑲夌溂鐚滄祴锛岀洿鎺ユ牴鎹槑纭殑灞傜骇鍙橀噺鍐冲畾鍒楄〃锛?
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

    // 鈹€鈹€ 椤堕儴鐘舵€佹爮 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
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

    // 馃幆 鏄庣‘澶勪簬浜岀骇鑿滃崟灞傦紝鎵嶆敼鍚?
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

    // 馃幆 鏄庣‘澶勪簬浜岀骇鑿滃崟灞傦紝鎵嶆敼鍚嶅拰鍥炬爣
    if (menu_layer == 2 && selected == MODE_GAME_SELECT) {
        cur_name = "Back";
        cur_icon = 66; // 杩斿洖绠ご
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
    const char *wifi_text = wifi_manager_is_connect() ? "Wi-Fi 已连接" : "未连接 Wi-Fi";
    const char *state_text = "空闲";
    uint32_t ms_now = (uint32_t)(esp_timer_get_time() / 1000);
    char info_text[32];

    switch (state) {
        case AUDIO_STATE_BUFFERING:
            state_text = "缓冲中";
            break;
        case AUDIO_STATE_PLAYING:
            state_text = "播放中";
            break;
        case AUDIO_STATE_ERROR:
            state_text = "错误";
            break;
        case AUDIO_STATE_NO_WIFI:
            state_text = "未连接 Wi-Fi";
            break;
        case AUDIO_STATE_IDLE:
        default:
            state_text = "空闲";
            break;
    }

    u8g2_ClearBuffer(u8g2);
    u8g2_SetFont(u8g2, u8g2_font_wqy12_t_gb2312);
    u8g2_DrawUTF8(u8g2, 28, 11, "网络电台");
    u8g2_DrawHLine(u8g2, 0, 12, 128);

    u8g2_DrawUTF8(u8g2, 2, 24, wifi_text);
    u8g2_DrawUTF8(u8g2, 2, 36, state_text);
    snprintf(info_text, sizeof(info_text), "音量:%u%%", volume);
    u8g2_DrawUTF8(u8g2, 62, 36, info_text);

    u8g2_DrawFrame(u8g2, 2, 40, 124, 12);
    u8g2_SetClipWindow(u8g2, 4, 40, 124, 52);
    int text_w = u8g2_GetUTF8Width(u8g2, station->name);
    int scroll_x = 6;
    if (text_w > 116) {
        scroll_x = 6 - ((ms_now / 120) % (text_w + 16));
    }
    u8g2_DrawUTF8(u8g2, scroll_x, 49, station->name);
    u8g2_SetMaxClipWindow(u8g2);

    u8g2_DrawHLine(u8g2, 0, 55, 128);
    u8g2_DrawUTF8(u8g2, 2, 63, "短按切台");
    u8g2_DrawUTF8(u8g2, 74, 63, "长按菜单");
    u8g2_SendBuffer(u8g2);
}

/**
 * @brief 缁樺埗鎮诞鐞冩父鎴忕晫闈?
 * @param u8g2 灞忓箷鍙ユ焺鎸囬拡
 */
// 杈呭姪鍑芥暟锛氬垽鏂偣 (px, py) 鏄惁鍦ㄧ煩褰㈤殰纰嶇墿鍐?
static bool is_point_in_box(float px, float py, Obstacle_t box, float margin) {
    return (px + margin > box.x && px - margin < box.x + box.w &&
            py + margin > box.y && py - margin < box.y + box.h);
}

void draw_ball_game(u8g2_t *u8g2) 
{
    static float x = 20.0f, y = 20.0f; // 鐞冧綅缃?
    static float vx = 0.0f, vy = 0.0f;
    static int score = 0;

    static Obstacle_t wall = {54, 22, 20, 20}; // 闅滅鐗?
    static Coin_t coin = {100, 32, 3, true};    // 閲戝竵

    const uint8_t radius = 4;

    // ==========================================
    // 1. 鍚冮噾甯佷笌闅忔満浣嶇疆鍒锋柊閫昏緫
    // ==========================================
    if (coin.active) {
        float dx = x - coin.x;
        float dy = y - coin.y;
        if ((dx * dx + dy * dy) < (radius + coin.r) * (radius + coin.r)) {
            score++;
            
            // --- 闅忔満鐢熸垚鏂扮殑闅滅鐗╀綅缃?(閬垮紑褰撳墠灏忕悆) ---
            do {
                // esp_random() 杩斿洖 32 浣嶆棤绗﹀彿鏁帮紝鍙栦綑闄愬埗鍦ㄥ睆骞曞畨鍏ㄥ尯鍐?
                wall.x = (esp_random() % (128 - 30)) + 5; // 瀹藉害20锛岀暀杈圭晫
                wall.y = (esp_random() % (64 - 30)) + 5;
                wall.w = (esp_random() % 15) + 15; // 闅忔満瀹藉害 15~30
                wall.h = (esp_random() % 15) + 15; // 闅忔満楂樺害 15~30
            } while (is_point_in_box(x, y, wall, radius + 10)); // 濡傛灉闅滅鐗╁埛鍦ㄧ悆韬笂锛岄噸鏂扮敓鎴?

            // --- 闅忔満鐢熸垚鏂伴噾甯佷綅缃?(閬垮紑鐞冨拰鍒氱敓鎴愮殑闅滅鐗? ---
            do {
                coin.x = (esp_random() % (128 - 10)) + 5;
                coin.y = (esp_random() % (64 - 10)) + 5;
            } while (is_point_in_box(coin.x, coin.y, wall, radius + 5) || // 涓嶈兘鍦ㄥ閲?
                     ((coin.x - x) * (coin.x - x) + (coin.y - y) * (coin.y - y)) < 900); // 璺濈鐞冭嚦灏?30 鍍忕礌

            coin.active = true;
        }
    }

    // ==========================================
    // 2. 鐗╃悊杩愬姩婕旂畻
    // ==========================================
    vx = (vx - euler_angle.roll * 0.15f) * 0.92f; 
    vy = (vy + euler_angle.pitch * 0.15f) * 0.92f;
    
    float next_x = x + vx;
    float next_y = y + vy;

    // AABB 纰版挒妫€娴?
    if (next_x + radius > wall.x && next_x - radius < wall.x + wall.w &&
        y + radius > wall.y && y - radius < wall.y + wall.h) {
        vx = -vx * 0.5f;     // 姘村钩鏂瑰悜鐨勭Щ鍔?
        next_x = x;      
    }
    if (x + radius > wall.x && x - radius < wall.x + wall.w &&
        next_y + radius > wall.y && next_y - radius < wall.y + wall.h) {
        vy = -vy * 0.5f;    // 鍨傜洿鏂瑰悜鐨勭Щ鍔?
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
    .obs = { .x = 128, .type = 0, .bird_y = 35, .speed = 4.0f } // 鍒濆閫熷害涓?4.0
};

void dino_game_reset(DinoGame_t *game) 
{
    game->state = STATE_RUNNING;
    game->score = 0;
    
    // 鍒濆鍖栨亹榫?
    game->dino.y = 39.0f;
    game->dino.vy = 0.0f;
    game->dino.is_jumping = false;
    game->dino.is_ducking = false;
    
    // 鍒濆鍖栭殰纰嶇墿
    game->obs.x = 128;
    game->obs.type = 0;
    game->obs.speed = 4;
}

void draw_dino_game(u8g2_t *u8g2) 
{
    if (in_select) return; // 濡傛灉澶勪簬鑿滃崟鍒囨崲妯″紡锛岀洿鎺ヨ烦杩囨父鎴忕粯鍒?

// ==========================================
    // A. 娓告垙缁撴潫鐣岄潰鐘舵€?(STATE_GAMEOVER)
    // ==========================================
    if (dino_game.state == STATE_GAMEOVER) {
        u8g2_ClearBuffer(u8g2);
        int w; // 鐢ㄤ簬鍔ㄦ€佸瓨鏀惧瓧绗︿覆鍍忕礌瀹藉害

        // 1. 缁樺埗 "GAME OVER" (浣跨敤 6x12 瀛椾綋)
        u8g2_SetFont(u8g2, u8g2_font_6x12_tr);
        w = u8g2_GetStrWidth(u8g2, "GAME OVER");
        u8g2_DrawStr(u8g2, (128 - w) / 2, 22, "GAME OVER");
        
        // 2. 缁樺埗褰撳墠鍒嗘暟 (浣跨敤 6x10 瀛椾綋锛岃瀹冪◢寰樉鐪间竴鐐?
        char score_buf[20];
        snprintf(score_buf, sizeof(score_buf), "Score: %d", (int)dino_game.score);
        u8g2_SetFont(u8g2, u8g2_font_6x10_tr);
        w = u8g2_GetStrWidth(u8g2, score_buf);
        u8g2_DrawStr(u8g2, (128 - w) / 2, 36, score_buf);

        // 3. 缁樺埗鎸夐敭鎻愮ず (浣跨敤 6x10 瀛椾綋锛屼繚鎸佸伐鏁村害)
        u8g2_SetFont(u8g2, u8g2_font_6x10_tr);
        
        w = u8g2_GetStrWidth(u8g2, "SHORT KEY: RETRY");
        u8g2_DrawStr(u8g2, (128 - w) / 2, 50, "SHORT KEY: RETRY");

        w = u8g2_GetStrWidth(u8g2, "LONG KEY: MENU");
        u8g2_DrawStr(u8g2, (128 - w) / 2, 62, "LONG KEY: MENU");
        
        u8g2_SendBuffer(u8g2);
        return;
    }

    // ==========================================
    // B. 娓告垙杩愯閫昏緫婕旂畻 (STATE_RUNNING)
    // ==========================================

    // 1. MPU6050 濮挎€佹帶鍒惰鍙?
    if (!dino_game.dino.is_jumping && !dino_game.dino.is_ducking && euler_angle.pitch > 25.0f) {
        dino_game.dino.is_jumping = true; 
        dino_game.dino.vy = -5.5f; // 璧疯烦鍒濋€熷害
    }
    dino_game.dino.is_ducking = (euler_angle.pitch < -20.0f);

    // 2. 鎭愰緳鐗╃悊寮曟搸璁＄畻 (璺宠穬涓庨檷钀?
    if (dino_game.dino.is_jumping) {
        dino_game.dino.vy += 0.65f; // 閲嶅姏鍔犻€熷害
        dino_game.dino.y += dino_game.dino.vy;
        if (dino_game.dino.y >= 39.0f) { // 钀藉湴妫€娴?
            dino_game.dino.y = 39.0f;
            dino_game.dino.vy = 0.0f;
            dino_game.dino.is_jumping = false;
        }
    }

    // 3. 鍒嗘暟澧炲姞涓庛€愰€熷害鍔ㄦ€佸姞蹇€戦€昏緫
    dino_game.score += 0.2f; // 鍩轰簬鐢熷瓨鏃堕棿鐨勮窇閰疯鍒?
    
    // 鍩虹閫熷害 4.0锛屾瘡璺?100 鍒嗛€熷害澧炲姞 0.5銆傚皝椤堕€熷害 9.0锛岄槻姝㈠揩鍒颁汉绫绘棤娉曞弽搴?
    dino_game.obs.speed = 4.0f + (dino_game.score / 200.0f);
    if (dino_game.obs.speed > 9.0f) {
        dino_game.obs.speed = 9.0f; 
    }

    // 4. 闅滅鐗╁悜宸︽帹杩?
    dino_game.obs.x -= (int)dino_game.obs.speed; 
    if (dino_game.obs.x < -20) {
        dino_game.obs.x = 128; // 绉诲嚭灞忓箷鍚庨噸缃埌鏈€鍙充晶
        dino_game.obs.type = esp_random() % 2; // 闅忔満 0: 浠欎汉鎺? 1: 椋為笩
        dino_game.obs.bird_y = (esp_random() % 15) + 30; // 椋為笩闅忔満椋炶楂樺害 (30~45)
    }

    // ==========================================
    // C. 鏍稿績纰版挒妫€娴?(AABB 鐭╁舰纰版挒)
    // ==========================================
    
    // 鎭愰緳 Hitbox 鎻愬彇
    int d_x = 15;
    int d_y = (int)dino_game.dino.y;
    int d_w = 12;
    int d_h = (dino_game.dino.is_ducking ? 8 : 14); // 瓒翠笅鏃堕珮搴﹀彉鐭?
    if (dino_game.dino.is_ducking) d_y += 6; // 瓒翠笅鏃剁鎾炵涓嬫矇

    // 闅滅鐗?Hitbox 鎻愬彇
    int o_x = dino_game.obs.x;
    int o_y, o_w, o_h;
    if (dino_game.obs.type == 0) { // 浠欎汉鎺?
        o_y = 40; o_w = 8; o_h = 15;
    } else { // 椋為笩
        o_y = dino_game.obs.bird_y; o_w = 12; o_h = 6;
    }

    // 缁忓吀鐨?AABB 纰版挒鍒ゆ柇
    if (d_x < o_x + o_w && d_x + d_w > o_x &&
        d_y < o_y + o_h && d_y + d_h > o_y) {
        
        dino_game.state = STATE_GAMEOVER; // 鍙戠敓纰版挒锛岀姸鎬佸垏鍒版浜?
        if ((int)dino_game.score > dino_game.high_score) {
            dino_game.high_score = (int)dino_game.score;
        }
    }

    // ==========================================
    // D. OLED UI 鐢婚潰娓叉煋
    // ==========================================
    u8g2_ClearBuffer(u8g2);

    // 1. 鐢诲湴闈?
    u8g2_DrawHLine(u8g2, 0, 55, 128);

    // 2. 鐢绘亹榫?
    if (dino_game.dino.is_ducking && !dino_game.dino.is_jumping) {
        u8g2_DrawBox(u8g2, d_x, d_y, 18, 5); // 瓒翠笅褰㈡€?
    } else {
        u8g2_DrawBox(u8g2, d_x + 4, d_y, 10, 6); // 澶?
        u8g2_DrawBox(u8g2, d_x, d_y + 4, 12, 8); // 韬綋
        
        // 璺戞杩堣吙鍔ㄧ敾锛堟牴鎹殰纰嶇墿X鍧愭爣鐨勫鍋跺垽鏂紝鍒囨崲宸﹀彸鑴氶棯鐑侊級
        if (!dino_game.dino.is_jumping && (dino_game.obs.x % 10 < 5)) {
            u8g2_DrawVLine(u8g2, d_x + 3, d_y + 12, 4);
        } else if (!dino_game.dino.is_jumping) {
            u8g2_DrawVLine(u8g2, d_x + 8, d_y + 12, 4);
        } else {
            u8g2_DrawVLine(u8g2, d_x + 3, d_y + 12, 3); // 璺宠穬鏃舵洸鑵?
            u8g2_DrawVLine(u8g2, d_x + 8, d_y + 12, 3);
        }
    }

    // 3. 鐢婚殰纰嶇墿
    if (dino_game.obs.type == 0) { // 浠欎汉鎺?
        u8g2_DrawBox(u8g2, dino_game.obs.x + 2, 40, 4, 15);
        u8g2_DrawBox(u8g2, dino_game.obs.x, 44, 2, 6);
        u8g2_DrawBox(u8g2, dino_game.obs.x + 6, 42, 2, 6);
    } else { // 椋為笩
        u8g2_DrawBox(u8g2, dino_game.obs.x, dino_game.obs.bird_y, 12, 4); // 楦熻韩
        if (dino_game.obs.x % 12 < 6) { // 缈呰唨鎷嶆墦鍔ㄧ敾
            u8g2_DrawHLine(u8g2, dino_game.obs.x + 3, dino_game.obs.bird_y - 2, 6); // 缈呰唨鏈濅笂
        } else {
            u8g2_DrawHLine(u8g2, dino_game.obs.x + 3, dino_game.obs.bird_y + 5, 6); // 缈呰唨鏈濅笅
        }
    }

    // 4. 鐢诲彸涓婅璁″垎鏉?
    char buf[20];
    snprintf(buf, sizeof(buf), "HI %04d  %04d", dino_game.high_score, (int)dino_game.score);
    u8g2_SetFont(u8g2, u8g2_font_4x6_tf);
    u8g2_DrawStr(u8g2, 75, 8, buf);

    u8g2_SendBuffer(u8g2);
}

// 鍒濆鍖栧叏灞€娓告垙瀹炰緥
AirGame_t air_game = {
    .state = STATE_RUNNING,
    .score = 0,
    .player = { .x = 64, .y = 50, .vx = 0, .vy = 0, .width = 11, .height = 9 },
    
    // 鏁扮粍閲岀殑姣忎竴涓厓绱犻兘鏄竴涓?{}
    .bullets = { {0}, {0}, {0}, {0}, {0} }, 
    .enemies = { {0}, {0}, {0} }
};

void air_game_reset(AirGame_t *game) 
{
    // 1. 灏嗘暣涓粨鏋勪綋鍐呭瓨娓呴浂锛堝寘鎷瓙寮广€佹晫鏈烘暟缁勶級
    memset(game, 0, sizeof(AirGame_t));

    // 2. 璧嬩簣娓告垙杩愯鐘舵€?
    game->state = STATE_RUNNING;
    game->score = 0;

    // 3. 璧嬩簣鐜╁鍒濆鏁版嵁
    game->player.x = 64;
    game->player.y = 50;
    game->player.width = 11;
    game->player.height = 9;
}

void draw_plane_game(u8g2_t *u8g2)
{
    if (in_select) return;

    // ==========================================
    // A. 娓告垙缁撴潫鐣岄潰鐘舵€?(STATE_GAMEOVER)
    // ==========================================
    if (air_game.state == STATE_GAMEOVER) {
        u8g2_ClearBuffer(u8g2);
        int w; // 鐢ㄤ簬涓存椂瀛樻斁瀛楃涓插儚绱犲搴?

        // 1. 缁樺埗 "GAME OVER" (浣跨敤 6x12 瀛椾綋)
        u8g2_SetFont(u8g2, u8g2_font_6x12_tr);
        w = u8g2_GetStrWidth(u8g2, "GAME OVER");
        u8g2_DrawStr(u8g2, (128 - w) / 2, 22, "GAME OVER"); // Y 鍧愭爣绋嶅井鎻愪竴涓?
        
        // 2. 缁樺埗鍒嗘暟
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
    // B. MPU6050 鎴樻満鐗╃悊鎺у埗婕旂畻 (缁撳悎浣犲枩娆㈢殑娣锋惌鎵嬫劅)
    // ==========================================
    
    // 淇敼鎴樻満绉诲姩鎵嬫劅
    // X 杞达細姘旀场妯″紡锛堝摢杈归珮寰€鍝竟椋烇級锛?Y 杞达細閲嶅姏妯″紡锛堝摢杈逛綆寰€鍝竟婊戯級
    air_game.player.vx = (air_game.player.vx - euler_angle.roll * 0.18f) * 0.94f;
    air_game.player.vy = (air_game.player.vy + euler_angle.pitch * 0.18f) * 0.94f;

    // 濡傛灉浼犳劅鍣ㄦ暟鎹緢灏忥紙姣斿灏忎簬1搴︼級锛岀洿鎺ヨ涓?锛岄槻姝㈡墜鎶栧鑷撮鏈轰竴鐩存紓绉?
    if (fabs(euler_angle.roll) < 1.5f) air_game.player.vx *= 0.8f; 
    if (fabs(euler_angle.pitch) < 1.5f) air_game.player.vy *= 0.8f;

    air_game.player.x += air_game.player.vx;
    air_game.player.y += air_game.player.vy;

    // 灞忓箷杈圭晫闄愬埗
    if (air_game.player.x < 5) air_game.player.x = 5;
    if (air_game.player.x > 123) air_game.player.x = 123;
    if (air_game.player.y < 5) air_game.player.y = 5;
    if (air_game.player.y > 59) air_game.player.y = 59;

    // ==========================================
    // C. 娓告垙鍐呴儴閫昏緫婕旂畻 (瀛愬脊銆佹晫鏈?
    // ==========================================

    // 1. 鑷姩鍙戝皠瀛愬脊 (鍩轰簬甯ц鏁?鏃堕棿)
    static int fire_tick = 0;
    fire_tick++;
    if (fire_tick >= 2) { // 姣?2 甯у彂灏勪竴棰?
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

    // 瀛愬脊鍚戜笂绉诲姩
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (air_game.bullets[i].active) {
            air_game.bullets[i].y -= 3;
            if (air_game.bullets[i].y < 0) air_game.bullets[i].active = false;
        }
    }

    // 2. 鏁屾満涓嬭惤閫昏緫
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!air_game.enemies[i].active) { // 鍒锋柊鏁屾満
            air_game.enemies[i].x = (esp_random() % 110) + 10;
            air_game.enemies[i].y = -(esp_random() % 30);
            air_game.enemies[i].speed = (esp_random() % 2) + 1;
            air_game.enemies[i].active = true;
        } else {
            air_game.enemies[i].y += air_game.enemies[i].speed;
            if (air_game.enemies[i].y > 64) air_game.enemies[i].active = false; // 椋炲嚭搴曡竟鐣?
        }
    }

    // 3. 瀛愬脊涓庢晫鏈虹殑纰版挒 (Hitbox: 鏁屾満 8x8, 瀛愬脊 1x3)
    for (int b = 0; b < MAX_BULLETS; b++) {
        if (!air_game.bullets[b].active) continue;
        for (int e = 0; e < MAX_ENEMIES; e++) {
            if (!air_game.enemies[e].active) continue;

            if (air_game.bullets[b].x > air_game.enemies[e].x - 4 && air_game.bullets[b].x < air_game.enemies[e].x + 4 &&
                air_game.bullets[b].y > air_game.enemies[e].y - 4 && air_game.bullets[b].y < air_game.enemies[e].y + 4) {
                
                air_game.bullets[b].active = false;
                air_game.enemies[e].active = false; // 鍑绘瘉
                air_game.score += 10;
            }
        }
    }

    // 4. 鏁屾満涓庣帺瀹舵垬鏈虹殑纰版挒 (鐜╁ 11x9, 鏁屾満 8x8)
    for (int e = 0; e < MAX_ENEMIES; e++) {
        if (!air_game.enemies[e].active) continue;
        if (air_game.player.x - 5 < air_game.enemies[e].x + 4 && air_game.player.x + 5 > air_game.enemies[e].x - 4 &&
            air_game.player.y - 4 < air_game.enemies[e].y + 4 && air_game.player.y + 4 > air_game.enemies[e].y - 4) {
            
            air_game.state = STATE_GAMEOVER; // 鍧犳瘉
        }
    }

    // ==========================================
    // D. OLED UI 鐢诲竷娓叉煋
    // ==========================================
    u8g2_ClearBuffer(u8g2);

    // 1. 鐢绘槦绌鸿儗鏅紙鍑犻闅忔満娴佹槦涓嬫粦锛?
    static int star_y = 0;
    star_y = (star_y + 1) % 64;
    u8g2_DrawPixel(u8g2, 20, star_y);
    u8g2_DrawPixel(u8g2, 80, (star_y + 30) % 64);
    u8g2_DrawPixel(u8g2, 110, (star_y + 10) % 64);

    // 2. 缁樺埗鎴樻満 (涓夎褰㈡嫾鎺?
    int px = (int)air_game.player.x;
    int py = (int)air_game.player.y;
    u8g2_DrawTriangle(u8g2, px, py - 5, px - 5, py + 4, px + 5, py + 4); // 鏈鸿韩
    u8g2_DrawHLine(u8g2, px - 8, py + 2, 17); // 鏈虹考

    // 3. 缁樺埗鏁屾満 (鍍忕礌鍗佸瓧灏忔晫鏈?
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (air_game.enemies[i].active) {
            int ex = air_game.enemies[i].x;
            int ey = air_game.enemies[i].y;
            u8g2_DrawBox(u8g2, ex - 3, ey - 3, 7, 7); // 鏁屾満鏍稿績
            u8g2_DrawHLine(u8g2, ex - 5, ey, 11);
        }
    }

    // 4. 缁樺埗瀛愬脊
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (air_game.bullets[i].active) {
            u8g2_DrawVLine(u8g2, air_game.bullets[i].x, air_game.bullets[i].y, 3);
        }
    }

    // 5. 寰楀垎鏉?
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

    // 鈹€鈹€ 椤堕儴鏍囬 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf); //
    u8g2_DrawStr(u8g2, 25, 9, "Health Monitor"); //
    u8g2_DrawHLine(u8g2, 0, 11, 128); //
    u8g2_DrawHLine(u8g2, 0, 13, 128); //

    switch (b_state) //
    {
        case BLOOD_IDLE: //
            // 鎻愮ず鏀炬墜鎸?
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
            // 鏄剧ず蹇冪巼
            u8g2_SetFont(u8g2, u8g2_font_5x7_tf); //
            u8g2_DrawStr(u8g2, 2, 26, "Heart Rate"); //
            u8g2_SetFont(u8g2, u8g2_font_logisoso16_tn); //
            snprintf(buf, sizeof(buf), "%3d", b_data.heart); //
            u8g2_DrawStr(u8g2, 2, 44, buf); //
            u8g2_SetFont(u8g2, u8g2_font_5x7_tf); //
            u8g2_DrawStr(u8g2, 52, 44, "bpm"); //

            u8g2_DrawVLine(u8g2, 76, 15, 40); //

            // 鏄剧ず琛€姘?
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
        u8g2_SetFont(u8g2, u8g2_font_wqy12_t_gb2312);
        u8g2_DrawUTF8(u8g2, 28, 11, "音量调节");
        u8g2_DrawHLine(u8g2, 0, 12, 128);
        u8g2_DrawTriangle(u8g2, 18, 33, 28, 27, 28, 39);
        u8g2_DrawTriangle(u8g2, 110, 33, 100, 27, 100, 39);
        snprintf(buf, sizeof(buf), "%u%%", s_setting_preview_volume);
        u8g2_SetFont(u8g2, u8g2_font_logisoso16_tn);
        u8g2_DrawStr(u8g2, 32, 39, buf);
        u8g2_SetFont(u8g2, u8g2_font_wqy12_t_gb2312);
        u8g2_DrawUTF8(u8g2, 8, 54, "左减 右加 回正再触发");
        u8g2_DrawHLine(u8g2, 0, 55, 128);
        u8g2_DrawUTF8(u8g2, 20, 63, "长按保存并返回");
        u8g2_SendBuffer(u8g2);
        return;
    }

    if (s_setting_page == SETTING_PAGE_WIFI_RESET) {
        u8g2_SetFont(u8g2, u8g2_font_wqy12_t_gb2312);
        u8g2_DrawUTF8(u8g2, 28, 11, "重置 Wi-Fi");
        u8g2_DrawHLine(u8g2, 0, 12, 128);
        u8g2_DrawUTF8(u8g2, 10, 28, "将清除已保存网络");
        u8g2_DrawUTF8(u8g2, 16, 42, "长按立即执行重置");
        u8g2_DrawHLine(u8g2, 0, 55, 128);
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
