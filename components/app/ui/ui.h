#ifndef __UI_H
#define __UI_H
#include "u8g2.h"

typedef enum {
    MODE_CLOCK = 0,    // 主界面：时钟+传感器
    MODE_SELECT,       // 模式选择页面（短按切换游戏）
    MODE_BALL,         // 悬浮球
    MODE_DINO,         // 恐龙快跑
    MODE_PLANE,        // 飞机大战
    MODE_BLOOD,        // 血氧检测模式
} ui_mode_e;

// 选择列表，包含主时钟
static const ui_mode_e game_list[] = {
    MODE_CLOCK,   // 主时钟也在列表里
    MODE_BALL,
    MODE_DINO,
    MODE_PLANE,
    MODE_BLOOD,
};

/*
    悬浮球
*/
// 定义金币和障碍物结构体
typedef struct {
    int x, y, w, h;
} Obstacle_t;

/*
    恐龙快跑
*/
// 定义恐龙快跑对象
typedef struct {
    int x, y, r;
    bool active;
} Coin_t;

typedef enum {
    STATE_RUNNING,
    STATE_GAMEOVER
} GameState_t;

// 恐龙对象
typedef struct {
    float y;
    float vy;
    bool is_jumping;
    bool is_ducking;
} Dino_t;

// 障碍物对象
typedef struct {
    int x;
    int type;       // 0: 仙人掌, 1: 飞鸟
    int bird_y;     // 飞鸟的随机高度
    float speed;    // 移动速度（用 float 支持平滑微调）
} Din_Obstacle_t;

// 游戏总控
typedef struct {
    GameState_t state;
    float score;
    int high_score;
    Dino_t dino;
    Din_Obstacle_t obs;
} DinoGame_t;

/*
    飞机大战
*/
// 射速 & 子弹数
#define MAX_BULLETS 25
// 同时出现的敌机数
#define MAX_ENEMIES 3

// 战机对象
typedef struct {
    float x, y;
    float vx, vy;
    int width, height;
} Plane_t;

// 子弹对象
typedef struct {
    int x, y;
    bool active;
} Bullet_t;

// 敌机对象
typedef struct {
    int x, y;
    bool active;
    int speed;
} Enemy_t;

// 游戏总控
typedef struct {
    GameState_t state;
    int score;
    Plane_t player;
    Bullet_t bullets[MAX_BULLETS];
    Enemy_t enemies[MAX_ENEMIES];
} AirGame_t;

void draw_syncing_ui(u8g2_t *u8g2);

void draw_select_ui(u8g2_t *u8g2, ui_mode_e selected);

void draw_main_clock_ui(u8g2_t *u8g2);

void draw_ball_game(u8g2_t *u8g2);

void draw_dino_game(u8g2_t *u8g2);
void dino_game_reset(DinoGame_t *game);

void draw_plane_game(u8g2_t *u8g2);
void air_game_reset(AirGame_t *game);

void draw_blood_ui(u8g2_t *u8g2);

// 声明全局游戏对象
extern ui_mode_e mode;
extern ui_mode_e selected_game;  // 当前选中的游戏
extern DinoGame_t dino_game;
extern AirGame_t air_game;

#endif