
#include "headfile.h"
      
ui_mode_e mode = MODE_CLOCK;
ui_mode_e selected_game = MODE_BALL;

void draw_syncing_ui(u8g2_t *u8g2)
{
    static const char *tips[] = {
        "Never Give Up",
        "A New Beginning",
        "Code Changes World",
        "Keep On Loving"
    };

    uint32_t ms_now = esp_timer_get_time() / 1000; 

    u8g2_ClearBuffer(u8g2);

    // ── 顶部状态栏 ───────────────────────────────────────────
    u8g2_SetFont(u8g2, u8g2_font_6x12_tf); 
    u8g2_DrawStr(u8g2, 22, 12, "Syncing Time"); // 稍微左移，腾出右侧空间

    // 1. 动态省略号（缩短点数，防止撞到计时器）
    int dot_idx = (ms_now / 500) % 4; 
    for(int i = 0; i < dot_idx; i++) {
        u8g2_DrawStr(u8g2, 34 + (i * 4), 12, ".");
    }

    // 2. 新增：连接用时显示（靠右对齐）
    char time_buf[16];
    int seconds = ms_now / 1000;
    snprintf(time_buf, sizeof(time_buf), "%ds", seconds);
    int time_w = u8g2_GetStrWidth(u8g2, time_buf);
    u8g2_DrawStr(u8g2, 126 - time_w, 12, time_buf); // 距离右边缘保留 2 像素

    u8g2_DrawHLine(u8g2, 0, 16, 128); 

    // ── 中部：励志语 ────────────────────────────────────────
    int tip_idx = (ms_now / 3000) % 4; 
    u8g2_SetFont(u8g2, u8g2_font_7x14_tf); 
    int str_width = u8g2_GetStrWidth(u8g2, tips[tip_idx]);
    u8g2_DrawStr(u8g2, (128 - str_width) / 2, 38, tips[tip_idx]); 

    u8g2_DrawHLine(u8g2, 0, 48, 128); 

    // ── 底部：小球动画 (保持原样) ───────────────────────────
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
    char buf[32];
    time_t now;
    struct tm t;

    time(&now);
    localtime_r(&now, &t);

    u8g2_ClearBuffer(u8g2);

    // ── 顶部状态栏 ──────────────────────────────
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    sprintf(buf, "%04d-%02d-%02d", t.tm_year+1900, t.tm_mon+1, t.tm_mday);
    u8g2_DrawStr(u8g2, 2, 9, buf);

    const char *days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    u8g2_DrawStr(u8g2, 104, 9, days[t.tm_wday]);

    u8g2_DrawHLine(u8g2, 0, 11, 128);
    u8g2_DrawHLine(u8g2, 0, 13, 128);

    // ── 时间：时:分:秒 ───────────────────────────
    u8g2_SetFont(u8g2, u8g2_font_logisoso16_tn);
    sprintf(buf, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    u8g2_DrawStr(u8g2, 27, 34, buf);

    // ── 中部分割线（上移：53→37）─────────────────
    u8g2_DrawHLine(u8g2, 0, 37, 128);

    // ── 传感器区（整体上移约16px）────────────────
    u8g2_SetFont(u8g2, u8g2_font_5x7_tf);

    // 列1: 温度
    u8g2_DrawStr(u8g2, 2, 46, "TEMP");
    sprintf(buf, "%.1fC", bmp280.temperature);
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(u8g2, 2, 57, buf);

    // 列2: 气压
    u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
    u8g2_DrawStr(u8g2, 46, 46, "PRES");
    sprintf(buf, "%.0fhPa", bmp280.pressure);
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(u8g2, 44, 57, buf);

    // 列3: 步数
    u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
    u8g2_DrawStr(u8g2, 94, 46, "STEP");
    sprintf(buf, "%ldstp", step_data.today_steps);
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(u8g2, 90, 57, buf);

    // ── 底部双分割线（静态+动态） ──────────────────
    // 1. 较上的静态线
    u8g2_DrawHLine(u8g2, 0, 60, 128);
    // 2. 较下的动态线（15秒一轮，丝滑增长）
    uint32_t ms_now = esp_timer_get_time() / 1000;
    int progress_width = (ms_now % 15000) * 128 / 15000; // 计算 0-128 像素宽度
    u8g2_DrawHLine(u8g2, 0, 62, progress_width);

    u8g2_SendBuffer(u8g2);
}

// 建议把这个结构体定义拉到函数外面，防止每次调用函数都重新在栈上初始化
typedef struct {
    const char *name;
    uint16_t icon_code; // 改用 U8G2 内置字库的 Unicode 编码
} game_info_t;

void draw_select_ui(u8g2_t *u8g2, ui_mode_e selected)
{
    // 配置 App 信息（使用 u8g2_font_open_iconic_all_2x_t 图标库）
    static const game_info_t games[] = {
        [MODE_CLOCK] = {"Clock", 64 + 11}, // 钟表图标
        [MODE_BALL]  = {"Ball",  64 + 4},  // 球体/圆环
        [MODE_DINO]  = {"Dino",  64 + 23}, // 恐龙/小怪兽
        [MODE_PLANE] = {"Plane", 64 + 16}, // 飞机/飞行物
        [MODE_BLOOD]  = {"Spo2",  64 + 5},  // <--- 新增血氧，图标 64+5 是个漂亮的实心爱心❤️
    };

    static const ui_mode_e game_list[] = {
        MODE_CLOCK, MODE_BALL, MODE_DINO, MODE_PLANE, MODE_BLOOD // <--- 添加到轮播列表
    };
    
    // 自动计算 App 数量，防止手动填错越界
    const int game_count = sizeof(game_list) / sizeof(game_list[0]); 

    // 1. 寻找当前选中模式所在的索引 cur
    int cur = 0;
    for (int i = 0; i < game_count; i++) {
        if (game_list[i] == selected) { 
            cur = i; 
            break; 
        }
    }
    int left_i  = (cur - 1 + game_count) % game_count;
    int right_i = (cur + 1) % game_count;

    u8g2_ClearBuffer(u8g2);

    // ── 顶部状态栏 ─────────────────────────────
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(u8g2, 34, 10, "Select App");
    u8g2_DrawHLine(u8g2, 0, 12, 128);

    // ── 左右切换箭头（做成实心三角形，更精致） ───
    u8g2_DrawTriangle(u8g2, 4, 32, 8, 28, 8, 36);   // 左箭头 ◀
    u8g2_DrawTriangle(u8g2, 123, 32, 119, 28, 119, 36); // 右箭头 ▶

    // ── 左右侧文字（弱化显示） ───────────────────
    u8g2_SetFont(u8g2, u8g2_font_4x6_tf); // 侧边用更窄的字体，防止重叠
    
    // 左边App名称 (居左)
    u8g2_DrawStr(u8g2, 12, 35, games[game_list[left_i]].name);
    
    // 右边App名称 (向左靠齐，防止飞出屏幕)
    int right_name_w = u8g2_GetStrWidth(u8g2, games[game_list[right_i]].name);
    u8g2_DrawStr(u8g2, 116 - right_name_w, 35, games[game_list[right_i]].name);

    // ── 中间选中卡片（圆角矩形 + 居中） ─────────
    int center_x = 44, center_y = 15, w = 40, h = 34;
    u8g2_DrawRFrame(u8g2, center_x, center_y, w, h, 4);     // 圆角卡片外框
    
    // 绘制 16x16 居中图标
    u8g2_SetFont(u8g2, u8g2_font_open_iconic_all_2x_t); 
    u8g2_DrawGlyph(u8g2, center_x + 12, center_y + 24, games[selected].icon_code);

    // ── 选中文字：黑底白字（反色） ───────────────
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    int text_width = u8g2_GetStrWidth(u8g2, games[selected].name);
    int text_x = center_x + (w - text_width) / 2; // 文字水平居中
    
    // Y轴微调：保证卡片底部和反相文字贴合漂亮
    u8g2_DrawBox(u8g2, text_x - 2, center_y + h - 1, text_width + 4, 11); // 黑色背景条
    u8g2_SetDrawColor(u8g2, 0); // 开启反色 (写白色)
    u8g2_DrawStr(u8g2, text_x, center_y + h + 8, games[selected].name);
    u8g2_SetDrawColor(u8g2, 1); // 恢复正常画笔色

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
    static Coin_t coin = {100, 32, 3, true};    // 金币

    const uint8_t radius = 4;

    // ==========================================
    // 1. 吃金币与随机位置刷新逻辑
    // ==========================================
    if (coin.active) {
        float dx = x - coin.x;
        float dy = y - coin.y;
        if ((dx * dx + dy * dy) < (radius + coin.r) * (radius + coin.r)) {
            score++;
            
            // --- 随机生成新的障碍物位置 (避开当前小球) ---
            do {
                // esp_random() 返回 32 位无符号数，取余限制在屏幕安全区内
                wall.x = (esp_random() % (128 - 30)) + 5; // 宽度20，留边界
                wall.y = (esp_random() % (64 - 30)) + 5;
                wall.w = (esp_random() % 15) + 15; // 随机宽度 15~30
                wall.h = (esp_random() % 15) + 15; // 随机高度 15~30
            } while (is_point_in_box(x, y, wall, radius + 10)); // 如果障碍物刷在球身上，重新生成

            // --- 随机生成新金币位置 (避开球和刚生成的障碍物) ---
            do {
                coin.x = (esp_random() % (128 - 10)) + 5;
                coin.y = (esp_random() % (64 - 10)) + 5;
            } while (is_point_in_box(coin.x, coin.y, wall, radius + 5) || // 不能在墙里
                     ((coin.x - x) * (coin.x - x) + (coin.y - y) * (coin.y - y)) < 900); // 距离球至少 30 像素

            coin.active = true;
        }
    }

    // ==========================================
    // 2. 物理运动演算
    // ==========================================
    vx = (vx - euler_angle.roll * 0.15f) * 0.92f; 
    vy = (vy + euler_angle.pitch * 0.15f) * 0.92f;
    
    float next_x = x + vx;
    float next_y = y + vy;

    // AABB 碰撞检测
    if (next_x + radius > wall.x && next_x - radius < wall.x + wall.w &&
        y + radius > wall.y && y - radius < wall.y + wall.h) {
        vx = -vx * 0.5f;     // 水平方向的移动
        next_x = x;      
    }
    if (x + radius > wall.x && x - radius < wall.x + wall.w &&
        next_y + radius > wall.y && next_y - radius < wall.y + wall.h) {
        vy = -vy * 0.5f;    // 垂直方向的移动
        next_y = y;
    }

    x = next_x;
    y = next_y;

    // 边界限制
    if (x < radius) { x = radius; vx = -vx * 0.5f; }
    if (x > 128 - radius) { x = 128 - radius; vx = -vx * 0.5f; }
    if (y < radius) { y = radius; vy = -vy * 0.5f; }
    if (y > 64 - radius) { y = 64 - radius; vy = -vy * 0.5f; }

    // ==========================================
    // 3. UI 渲染
    // ==========================================
    u8g2_ClearBuffer(u8g2);
    u8g2_DrawFrame(u8g2, 0, 0, 128, 64); 
    
    u8g2_DrawBox(u8g2, wall.x, wall.y, wall.w, wall.h); // 随机大小的墙
    
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
    if (in_select) return; // 如果处于菜单切换模式，直接跳过游戏绘制

    // ==========================================
    // A. 游戏结束界面状态 (STATE_GAMEOVER)
    // ==========================================
    if (dino_game.state == STATE_GAMEOVER) {
        u8g2_ClearBuffer(u8g2);
        
        u8g2_SetFont(u8g2, u8g2_font_6x12_tr);
        u8g2_DrawStr(u8g2, 35, 22, "GAME OVER");
        
        char score_buf[20];
        snprintf(score_buf, sizeof(score_buf), "Score: %d", (int)dino_game.score);
        u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
        u8g2_DrawStr(u8g2, 40, 36, score_buf);

        u8g2_SetFont(u8g2, u8g2_font_4x6_tf);
        u8g2_DrawStr(u8g2, 31, 50, "SHORT KEY: RETRY");
        u8g2_DrawStr(u8g2, 33, 60, "LONG KEY : MENU");
        
        u8g2_SendBuffer(u8g2);
        return;
    }

    // ==========================================
    // B. 游戏运行逻辑演算 (STATE_RUNNING)
    // ==========================================

    // 1. MPU6050 姿态控制读取
    if (!dino_game.dino.is_jumping && !dino_game.dino.is_ducking && euler_angle.pitch > 25.0f) {
        dino_game.dino.is_jumping = true; 
        dino_game.dino.vy = -5.5f; // 起跳初速度
    }
    dino_game.dino.is_ducking = (euler_angle.pitch < -20.0f);

    // 2. 恐龙物理引擎计算 (跳跃与降落)
    if (dino_game.dino.is_jumping) {
        dino_game.dino.vy += 0.65f; // 重力加速度
        dino_game.dino.y += dino_game.dino.vy;
        if (dino_game.dino.y >= 39.0f) { // 落地检测
            dino_game.dino.y = 39.0f;
            dino_game.dino.vy = 0.0f;
            dino_game.dino.is_jumping = false;
        }
    }

    // 3. 分数增加与【速度动态加快】逻辑
    dino_game.score += 0.2f; // 基于生存时间的跑酷计分
    
    // 基础速度 4.0，每跑 100 分速度增加 0.5。封顶速度 9.0，防止快到人类无法反应
    dino_game.obs.speed = 4.0f + (dino_game.score / 200.0f);
    if (dino_game.obs.speed > 9.0f) {
        dino_game.obs.speed = 9.0f; 
    }

    // 4. 障碍物向左推进
    dino_game.obs.x -= (int)dino_game.obs.speed; 
    if (dino_game.obs.x < -20) {
        dino_game.obs.x = 128; // 移出屏幕后重置到最右侧
        dino_game.obs.type = esp_random() % 2; // 随机 0: 仙人掌, 1: 飞鸟
        dino_game.obs.bird_y = (esp_random() % 15) + 30; // 飞鸟随机飞行高度 (30~45)
    }

    // ==========================================
    // C. 核心碰撞检测 (AABB 矩形碰撞)
    // ==========================================
    
    // 恐龙 Hitbox 提取
    int d_x = 15;
    int d_y = (int)dino_game.dino.y;
    int d_w = 12;
    int d_h = (dino_game.dino.is_ducking ? 8 : 14); // 趴下时高度变矮
    if (dino_game.dino.is_ducking) d_y += 6; // 趴下时碰撞箱下沉

    // 障碍物 Hitbox 提取
    int o_x = dino_game.obs.x;
    int o_y, o_w, o_h;
    if (dino_game.obs.type == 0) { // 仙人掌
        o_y = 40; o_w = 8; o_h = 15;
    } else { // 飞鸟
        o_y = dino_game.obs.bird_y; o_w = 12; o_h = 6;
    }

    // 经典的 AABB 碰撞判断
    if (d_x < o_x + o_w && d_x + d_w > o_x &&
        d_y < o_y + o_h && d_y + d_h > o_y) {
        
        dino_game.state = STATE_GAMEOVER; // 发生碰撞，状态切到死亡
        if ((int)dino_game.score > dino_game.high_score) {
            dino_game.high_score = (int)dino_game.score;
        }
    }

    // ==========================================
    // D. OLED UI 画面渲染
    // ==========================================
    u8g2_ClearBuffer(u8g2);

    // 1. 画地面
    u8g2_DrawHLine(u8g2, 0, 55, 128);

    // 2. 画恐龙
    if (dino_game.dino.is_ducking && !dino_game.dino.is_jumping) {
        u8g2_DrawBox(u8g2, d_x, d_y, 18, 5); // 趴下形态
    } else {
        u8g2_DrawBox(u8g2, d_x + 4, d_y, 10, 6); // 头
        u8g2_DrawBox(u8g2, d_x, d_y + 4, 12, 8); // 身体
        
        // 跑步迈腿动画（根据障碍物X坐标的奇偶判断，切换左右脚闪烁）
        if (!dino_game.dino.is_jumping && (dino_game.obs.x % 10 < 5)) {
            u8g2_DrawVLine(u8g2, d_x + 3, d_y + 12, 4);
        } else if (!dino_game.dino.is_jumping) {
            u8g2_DrawVLine(u8g2, d_x + 8, d_y + 12, 4);
        } else {
            u8g2_DrawVLine(u8g2, d_x + 3, d_y + 12, 3); // 跳跃时曲腿
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
        if (dino_game.obs.x % 12 < 6) { // 翅膀拍打动画
            u8g2_DrawHLine(u8g2, dino_game.obs.x + 3, dino_game.obs.bird_y - 2, 6); // 翅膀朝上
        } else {
            u8g2_DrawHLine(u8g2, dino_game.obs.x + 3, dino_game.obs.bird_y + 5, 6); // 翅膀朝下
        }
    }

    // 4. 画右上角计分板
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
    
    // 数组里的每一个元素都是一个 {}
    .bullets = { {0}, {0}, {0}, {0}, {0} }, 
    .enemies = { {0}, {0}, {0} }
};

void air_game_reset(AirGame_t *game) 
{
    // 1. 将整个结构体内存清零（包括子弹、敌机数组）
    memset(game, 0, sizeof(AirGame_t));

    // 2. 赋予游戏运行状态
    game->state = STATE_RUNNING;
    game->score = 0;

    // 3. 赋予玩家初始数据
    game->player.x = 64;
    game->player.y = 50;
    game->player.width = 11;
    game->player.height = 9;
}

void draw_plane_game(u8g2_t *u8g2)
{
    if (in_select) return;

    // ==========================================
    // A. 游戏结束界面状态 (STATE_GAMEOVER)
    // ==========================================
    if (air_game.state == STATE_GAMEOVER) {
        u8g2_ClearBuffer(u8g2);
        u8g2_SetFont(u8g2, u8g2_font_6x12_tr);
        u8g2_DrawStr(u8g2, 40, 25, "GAME OVER");
        
        char buf[20];
        snprintf(buf, sizeof(buf), "SCORE: %d", air_game.score);
        u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
        u8g2_DrawStr(u8g2, 41, 36, buf);

        u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
        u8g2_DrawStr(u8g2, 22, 48, "SHORT KEY: RESTART");
        u8g2_DrawStr(u8g2, 30, 58, "LONG KEY: MENU");
        
        u8g2_SendBuffer(u8g2);
        return;
    }

    // ==========================================
    // B. MPU6050 战机物理控制演算 (结合你喜欢的混搭手感)
    // ==========================================
    
    // 修改战机移动手感
    // X 轴：气泡模式（哪边高往哪边飞）； Y 轴：重力模式（哪边低往哪边滑）
    air_game.player.vx = (air_game.player.vx - euler_angle.roll * 0.18f) * 0.94f;
    air_game.player.vy = (air_game.player.vy + euler_angle.pitch * 0.18f) * 0.94f;

    // 如果传感器数据很小（比如小于1度），直接视为0，防止手抖导致飞机一直漂移
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
    // C. 游戏内部逻辑演算 (子弹、敌机)
    // ==========================================

    // 1. 自动发射子弹 (基于帧计数/时间)
    static int fire_tick = 0;
    fire_tick++;
    if (fire_tick >= 2) { // 每 2 帧发射一颗
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
            if (air_game.enemies[i].y > 64) air_game.enemies[i].active = false; // 飞出底边界
        }
    }

    // 3. 子弹与敌机的碰撞 (Hitbox: 敌机 8x8, 子弹 1x3)
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

    // 4. 敌机与玩家战机的碰撞 (玩家 11x9, 敌机 8x8)
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

    // 1. 画星空背景（几颗随机流星下滑）
    static int star_y = 0;
    star_y = (star_y + 1) % 64;
    u8g2_DrawPixel(u8g2, 20, star_y);
    u8g2_DrawPixel(u8g2, 80, (star_y + 30) % 64);
    u8g2_DrawPixel(u8g2, 110, (star_y + 10) % 64);

    // 2. 绘制战机 (三角形拼接)
    int px = (int)air_game.player.x;
    int py = (int)air_game.player.y;
    u8g2_DrawTriangle(u8g2, px, py - 5, px - 5, py + 4, px + 5, py + 4); // 机身
    u8g2_DrawHLine(u8g2, px - 8, py + 2, 17); // 机翼

    // 3. 绘制敌机 (像素十字小敌机)
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

    // 5. 得分板
    char score_str[16];
    snprintf(score_str, sizeof(score_str), "SCORE:%04d", air_game.score);
    u8g2_SetFont(u8g2, u8g2_font_4x6_tf);
    u8g2_DrawStr(u8g2, 2, 8, score_str);

    u8g2_SendBuffer(u8g2);
}

void draw_blood_ui(u8g2_t *u8g2)
{
    char buf[32];
    u8g2_ClearBuffer(u8g2);

    // ── 顶部标题 ─────────────────────────────
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(u8g2, 30, 9, "Health Monitor");
    u8g2_DrawHLine(u8g2, 0, 11, 128);
    u8g2_DrawHLine(u8g2, 0, 13, 128);

    switch (b_state)
    {
        case BLOOD_IDLE:
            // 提示放手指
            u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
            u8g2_DrawStr(u8g2, 10, 35, "Place finger on");
            u8g2_DrawStr(u8g2, 20, 47, "the sensor...");

            // 动态闪烁的心形（用方块模拟）
            if ((xTaskGetTickCount() / 500) % 2 == 0) {
                u8g2_DrawBox(u8g2, 58, 53, 6, 6);
                u8g2_DrawBox(u8g2, 64, 53, 6, 6);
                u8g2_DrawBox(u8g2, 55, 56, 18, 4);
                u8g2_DrawBox(u8g2, 58, 60, 12, 3);
                u8g2_DrawBox(u8g2, 61, 63, 6, 2);
            }
            break;

        case BLOOD_SAMPLING:
        {
            // 显示采集进度
            u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
            u8g2_DrawStr(u8g2, 22, 30, "Measuring...");

            // 进度条动画（根据时间流动）
            static uint32_t sample_start = 0;
            if (sample_start == 0) sample_start = xTaskGetTickCount();
            uint32_t elapsed = xTaskGetTickCount() - sample_start;
            int bar = (int)(elapsed / 51);  // 5120ms总时长，128px宽
            if (bar > 128) bar = 128;

            u8g2_DrawFrame(u8g2, 0, 38, 128, 8);   // 进度条外框
            u8g2_DrawBox(u8g2, 0, 38, bar, 8);      // 进度填充

            u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
            u8g2_DrawStr(u8g2, 2, 56, "Keep still...");

            // 采集完重置起始时间
            if (b_state != BLOOD_SAMPLING) sample_start = 0;
            break;
        }

        case BLOOD_DONE:
            // 显示心率
            u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
            u8g2_DrawStr(u8g2, 2, 26, "Heart Rate");
            u8g2_SetFont(u8g2, u8g2_font_logisoso16_tn);
            snprintf(buf, sizeof(buf), "%3d", b_data.heart);
            u8g2_DrawStr(u8g2, 2, 44, buf);
            u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
            u8g2_DrawStr(u8g2, 52, 44, "bpm");

            // 分割线
            u8g2_DrawVLine(u8g2, 76, 15, 46);

            // 显示血氧
            u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
            u8g2_DrawStr(u8g2, 80, 26, "SpO2");
            u8g2_SetFont(u8g2, u8g2_font_logisoso16_tn);
            snprintf(buf, sizeof(buf), "%3d", (int)b_data.SpO2);
            u8g2_DrawStr(u8g2, 80, 44, buf);
            u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
            u8g2_DrawStr(u8g2, 112, 44, "%");

            // 底部健康评估
            u8g2_DrawHLine(u8g2, 0, 48, 128);
            u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
            if (b_data.SpO2 >= 95 && b_data.heart >= 60 && b_data.heart <= 100) {
                u8g2_DrawStr(u8g2, 20, 58, "Status: Normal");
            } else {
                u8g2_DrawStr(u8g2, 14, 58, "Status: Abnormal!");
            }
            break;
    }

    u8g2_SendBuffer(u8g2);
}