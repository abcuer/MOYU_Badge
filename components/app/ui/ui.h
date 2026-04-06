#ifndef __UI_H
#define __UI_H

#include <stdbool.h>
#include <stdint.h>

#include "u8g2.h"

typedef enum {
    MODE_CLOCK = 0,
    MODE_GAME_SELECT,
    MODE_RADIO, 
    MODE_BLOOD,
    MODE_SETTING,
    MODE_BALL,
    MODE_DINO,
    MODE_PLANE,
} ui_mode_e;

static const ui_mode_e main_app_list[] = {
    MODE_CLOCK,
    MODE_BLOOD,
    MODE_GAME_SELECT,
    MODE_RADIO,
    MODE_SETTING,
};

static const ui_mode_e sub_game_list[] = {
    MODE_BALL,
    MODE_DINO,
    MODE_PLANE,
    MODE_GAME_SELECT,
};

typedef struct {
    const char *name;
    uint16_t icon_code;
} game_info_t;

typedef enum {
    SETTING_ITEM_INFO = 0,
    SETTING_ITEM_VOLUME,
    SETTING_ITEM_WIFI_RESET,
    SETTING_ITEM_EXIT,
} setting_item_t;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} Obstacle_t;

typedef struct {
    int x;
    int y;
    int r;
    bool active;
} Coin_t;

typedef enum {
    STATE_RUNNING,
    STATE_GAMEOVER
} GameState_t;

typedef struct {
    float y;
    float vy;
    bool is_jumping;
    bool is_ducking;
} Dino_t;

typedef struct {
    int x;
    int type;
    int bird_y;
    float speed;
} Din_Obstacle_t;

typedef struct {
    GameState_t state;
    float score;
    int high_score;
    Dino_t dino;
    Din_Obstacle_t obs;
} DinoGame_t;

#define MAX_BULLETS 25
#define MAX_ENEMIES 3

typedef struct {
    float x;
    float y;
    float vx;
    float vy;
    int width;
    int height;
} Plane_t;

typedef struct {
    int x;
    int y;
    bool active;
} Bullet_t;

typedef struct {
    int x;
    int y;
    bool active;
    int speed;
} Enemy_t;

typedef struct {
    GameState_t state;
    int score;
    Plane_t player;
    Bullet_t bullets[MAX_BULLETS];
    Enemy_t enemies[MAX_ENEMIES];
} AirGame_t;

void draw_syncing_ui(u8g2_t *u8g2);
void reset_sync_ui_timer(void);
void draw_select_ui(u8g2_t *u8g2, ui_mode_e selected);
void draw_main_clock_ui(u8g2_t *u8g2);
void draw_radio_ui(u8g2_t *u8g2);
void draw_ball_game(u8g2_t *u8g2);
void draw_dino_game(u8g2_t *u8g2);
void dino_game_reset(DinoGame_t *game);
void draw_plane_game(u8g2_t *u8g2);
void air_game_reset(AirGame_t *game);
void draw_blood_ui(u8g2_t *u8g2);
void reset_blood_ui_timer(void);
void draw_setting_ui(u8g2_t *u8g2);
void setting_ui_reset_state(void);
bool setting_ui_handle_short_press(void);
bool setting_ui_handle_long_press(void);
bool setting_ui_is_volume_editing(void);
void setting_ui_update_volume_tilt(float roll);
void setting_ui_set_wifi_reset_armed(bool armed);
bool setting_ui_is_wifi_reset_armed(void);
void setting_ui_toggle_wifi_reset_armed(void);
bool radio_ui_is_volume_editing(void);
void radio_ui_toggle_volume_edit(void);
void radio_ui_exit_volume_edit(bool save);
void radio_ui_update_volume_tilt(float roll);

extern int menu_layer;
extern ui_mode_e mode;
extern ui_mode_e selected_game;
extern DinoGame_t dino_game;
extern AirGame_t air_game;

#endif
