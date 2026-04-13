#include "headfile.h"
static const uint32_t IDLE_SLEEP_TIMEOUT_MS = 35000;

bool in_select = false;
uint32_t last_action_time = 0;
static bool ignore_first_long_press_after_wake = false;
static bool s_radio_long_pending = false;
static volatile bool s_idle_network_sleeping = false;

extern TaskHandle_t sensor_task_handle;

static void app_mode_set(ui_mode_e next_mode)
{
    if (mode == MODE_RADIO && next_mode != MODE_RADIO) {
        s_radio_long_pending = false;
        if (radio_ui_is_volume_editing()) {
            radio_ui_exit_volume_edit(true);
        }
        audio_player_exit_radio_mode();
    }
    if (mode == MODE_RECORDER && next_mode != MODE_RECORDER) {
        recorder_exit_mode();
    }
    if (next_mode != MODE_SETTING) {
        setting_ui_reset_state();
    }
    mode = next_mode;
    if (mode == MODE_RADIO) {
        audio_player_enter_radio_mode();
    } else if (mode == MODE_RECORDER) {
        recorder_enter_mode();
    } else if (mode == MODE_SETTING) {
        setting_ui_reset_state();
    }
}

static bool mode_is_idle_sleep_page(void)
{
    if (mode == MODE_CLOCK) {
        return true;
    }

    if (in_select) {
        return true;
    }

    if (mode == MODE_GAME_SELECT) {
        return true;
    }

    if (mode == MODE_SETTING && setting_ui_is_info_page()) {
        return true;
    }

    return false;
}

void key_scan(void)
{
    key_event_e event = key_get_event(KEY_USER);

    if (s_idle_network_sleeping) {
        if (event != KEY_EVENT_NONE) {
            s_idle_network_sleeping = false;
            last_action_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            ignore_first_long_press_after_wake = true;
            s_radio_long_pending = false;
            key_reset_fsm(KEY_USER);
        }
        return;
    }

    if (!is_first_sync_done && ap_wifi_is_config_mode_active()) {
        if (event != KEY_EVENT_NONE) {
            last_action_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        }
        if (event == KEY_EVENT_LONG) {
            erase_wifi_from_nvs();
            esp_restart();
        }
        return;
    }

    const int main_app_count = sizeof(main_app_list) / sizeof(main_app_list[0]);
    const int sub_game_count = sizeof(sub_game_list) / sizeof(sub_game_list[0]);

    if (event != KEY_EVENT_NONE) {
        last_action_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    }

    if (ignore_first_long_press_after_wake) {
        if (event == KEY_EVENT_SHORT) {
            ignore_first_long_press_after_wake = false;
        } else if (event == KEY_EVENT_LONG || event == KEY_EVENT_SUPER_LONG) {
            ignore_first_long_press_after_wake = false;
            return;
        }
    }

    if (!(mode == MODE_RADIO && !in_select)) {
        s_radio_long_pending = false;
    }

    if (mode == MODE_RADIO && !in_select && s_radio_long_pending && key_is_idle(KEY_USER)) {
        s_radio_long_pending = false;
        radio_ui_toggle_volume_edit();
        return;
    }

    if (!in_select && mode == MODE_RADIO) {
        if (event == KEY_EVENT_SUPER_LONG) {
            s_radio_long_pending = false;
            if (radio_ui_is_volume_editing()) {
                radio_ui_exit_volume_edit(true);
            }
            app_mode_set(MODE_CLOCK);
            selected_game = MODE_RADIO;
            menu_layer = 1;
            in_select = true;
            return;
        }

        if (event == KEY_EVENT_LONG) {
            s_radio_long_pending = true;
            return;
        }

        if (event == KEY_EVENT_SHORT) {
            if (!radio_ui_is_volume_editing()) {
                audio_player_next_station();
            }
            return;
        }
    }

    if (!in_select && mode == MODE_RECORDER) {
        if (event == KEY_EVENT_SHORT) {
            recorder_handle_short_press();
            return;
        }

        if (event == KEY_EVENT_LONG && recorder_get_state() != RECORDER_STATE_IDLE) {
            recorder_stop_and_reset();
            return;
        }
    }

    if (event == KEY_EVENT_SHORT) {
        if (in_select) {
            if (menu_layer == 2) {
                int idx = 0;
                for (int i = 0; i < sub_game_count; i++) {
                    if (sub_game_list[i] == selected_game) {
                        idx = i;
                        break;
                    }
                }
                idx = (idx + 1) % sub_game_count;
                selected_game = sub_game_list[idx];
            } else {
                int idx = 0;
                for (int i = 0; i < main_app_count; i++) {
                    if (main_app_list[i] == selected_game) {
                        idx = i;
                        break;
                    }
                }
                idx = (idx + 1) % main_app_count;
                selected_game = main_app_list[idx];
            }
        } else {
            if (mode == MODE_SETTING) {
                if (setting_ui_handle_short_press()) {
                    return;
                }
            } else if (mode == MODE_DINO && dino_game.state == STATE_GAMEOVER) {
                dino_game_reset(&dino_game);
            } else if (mode == MODE_PLANE && air_game.state == STATE_GAMEOVER) {
                air_game_reset(&air_game);
            }
        }
    } else if (event == KEY_EVENT_LONG) {
        if (!in_select) {
            if (mode == MODE_SETTING && setting_ui_handle_long_press()) {
                return;
            }
            if (mode == MODE_SETTING) {
                setting_ui_reset_state();
            }
            selected_game = mode;
            in_select = true;

            if (mode == MODE_BALL || mode == MODE_DINO || mode == MODE_PLANE) {
                menu_layer = 2;
            } else {
                menu_layer = 1;
            }
            return;
        }

        if (in_select) {
            if (menu_layer == 2) {
                if (selected_game == MODE_GAME_SELECT) {
                    menu_layer = 1;
                    selected_game = MODE_GAME_SELECT;
                } else {
                    app_mode_set(selected_game);
                    app_mode_set(selected_game);
                    in_select = false;
                }
            } else {
                if (selected_game == MODE_GAME_SELECT) {
                    menu_layer = 2;
                    selected_game = MODE_BALL;
                } else {
                    app_mode_set(selected_game);
                    in_select = false;
                }
            }
            return;
        }
    }
}

void enter_light_sleep(void)
{
    if (s_idle_network_sleeping) {
        return;
    }

    s_idle_network_sleeping = true;
    u8g2_SetPowerSave(&u8g2, 1);
    mpu6050_sleep(1);
    bmp280_sleep(1);
    max30102_sleep(1);

    if (sensor_task_handle != NULL) {
        vTaskSuspend(sensor_task_handle);
    }

    while (s_idle_network_sleeping) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    u8g2_SetPowerSave(&u8g2, 0);
    sensor_request_power_sync();

    if (sensor_task_handle != NULL) {
        vTaskResume(sensor_task_handle);
    }

    ignore_first_long_press_after_wake = true;
    s_radio_long_pending = false;
    setting_ui_set_wifi_reset_armed(false);
    key_reset_fsm(KEY_USER);
    last_action_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
}

bool mode_try_enter_light_sleep(void)
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (!mode_is_idle_sleep_page()) {
        return false;
    }

    if ((now - last_action_time) <= IDLE_SLEEP_TIMEOUT_MS) {
        return false;
    }

    enter_light_sleep();
    return true;
}
